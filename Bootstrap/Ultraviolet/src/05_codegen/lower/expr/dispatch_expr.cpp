// =============================================================================
// MIGRATION MAPPING: expr/dispatch_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 10.5 (Dispatch - Data Parallelism)
//   - dispatch i in range { body } with optional reduce/ordered/chunk
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - DispatchExpr visitor produces IRDispatch
//   - Reduction handling for parallel aggregation
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRDispatch)
//
// =============================================================================

#include "05_codegen/lower/expr/dispatch_expr.h"

#include <cctype>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "00_core/spec_trace.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_pat.h"
#include "05_codegen/lower/lower_proc.h"
#include "05_codegen/lower/pattern/pattern_common.h"

namespace ultraviolet::codegen {

namespace {

// =============================================================================
// Helper Types and Functions
// =============================================================================

using CaptureBinding = ParallelCaptureBinding;

// ScopedNames tracks local names for capture collection.
struct ScopedNames {
  std::vector<std::unordered_set<std::string>> scopes;

  void PushScope() { scopes.emplace_back(); }
  void PopScope() {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }
  void Add(const std::string& name) {
    if (!scopes.empty()) {
      scopes.back().insert(name);
    }
  }
  bool IsLocal(const std::string& name) const {
    for (const auto& scope : scopes) {
      if (scope.find(name) != scope.end()) {
        return true;
      }
    }
    return false;
  }
};

// Permission extraction from type
analysis::Permission PermissionOfType(const analysis::TypeRef& type) {
  if (!type) {
    return analysis::Permission::Const;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return perm->perm;
  }
  return analysis::Permission::Const;
}

// Check if type is unit ()
bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

std::string DispatchModuleSuffix(const LowerCtx& ctx) {
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

std::string NextDispatchSynthSymbol(LowerCtx& ctx, std::string_view role) {
  return "__cx_dispatch_" + std::string(role) + "_" +
         DispatchModuleSuffix(ctx) + "_" +
         std::to_string(ctx.synth_proc_counter++);
}

// Create usize immediate value.
IRValue USizeImmediate(std::uint64_t value, const LowerCtx& ctx) {
  const auto env = ::ultraviolet::analysis::layout::LayoutEnvOf(
      ctx.target_profile.value_or(project::TargetProfile::X86_64SysV));
  const std::uint64_t ptr_size = ::ultraviolet::analysis::layout::PtrSize(env);
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = std::to_string(value);
  v.bytes.resize(static_cast<std::size_t>(ptr_size));
  for (std::uint64_t i = 0; i < ptr_size; ++i) {
    v.bytes[i] = i < sizeof(value)
                     ? static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF)
                     : 0;
  }
  return v;
}

// Create a null pointer via transmute
IRValue MakeNullPtr(const analysis::TypeRef& ptr_type,
                    LowerCtx& ctx,
                    std::vector<IRPtr>& parts) {
  IRValue zero = USizeImmediate(0, ctx);
  IRValue null_ptr = ctx.FreshTempValue("null_ptr");
  IRTransmute trans;
  trans.from = analysis::MakeTypePrim("usize");
  trans.to = ptr_type;
  trans.value = zero;
  trans.result = null_ptr;
  parts.push_back(MakeIR(std::move(trans)));
  ctx.RegisterValueType(null_ptr, ptr_type);
  return null_ptr;
}

// =============================================================================
// Pattern Name Collection
// =============================================================================

void CollectPatternNames(const ast::Pattern& pat, std::vector<std::string>& out);

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
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (const auto& elem : payload.elements) {
                      if (elem) {
                        CollectPatternNames(*elem, out);
                      }
                    }
                  } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                    for (const auto& field : payload.fields) {
                      CollectFieldPatNames(field, out);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (node.fields_opt) {
            for (const auto& field : node.fields_opt->fields) {
              CollectFieldPatNames(field, out);
            }
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
// Capture Collection Visitor
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
  void VisitPattern(const ast::PatternPtr& pat);
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
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          VisitExpr(node.cond);
          VisitExpr(node.then_expr);
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
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
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          VisitExpr(node.base);
          VisitExpr(node.index);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          MarkExplicitMove(node.place);
          VisitExpr(node.place);
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
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          VisitExpr(node.scrutinee);
          for (const auto& case_clause : node.cases) {
            locals.PushScope();
            VisitPattern(case_clause.pattern);
            VisitExpr(case_clause.body);
            locals.PopScope();
          }
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          VisitExpr(node.scrutinee);
          locals.PushScope();
          VisitPattern(node.pattern);
          VisitExpr(node.then_expr);
          locals.PopScope();
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
          locals.PushScope();
          VisitPattern(node.pattern);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.PopScope();
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          VisitExpr(node.value);
          if (node.region_opt) {
            RecordCapture(*node.region_opt);
          } else {
            MaybeCaptureImplicitRegion();
          }
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          VisitExpr(node.expr);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          VisitExpr(node.expr);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          VisitExpr(node.handle);
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          VisitExpr(node.domain);
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          VisitExpr(node.range);
          locals.PushScope();
          VisitPattern(node.pattern);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.PopScope();
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            VisitExpr(arm.expr);
            VisitExpr(arm.handler.value);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& e : node.exprs) {
            VisitExpr(e);
          }
        }
      },
      expr->node);
}

void CaptureCollector::VisitPattern(const ast::PatternPtr& pat) {
  if (!pat) {
    return;
  }
  std::vector<std::string> names;
  CollectPatternNames(*pat, names);
  for (const auto& name : names) {
    locals.Add(name);
  }
}

void CaptureCollector::VisitStmt(const ast::Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          VisitExpr(node.binding.init);
          if (node.binding.pat) {
            VisitPattern(node.binding.pat);
          }
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          if (node.binding.init) {
            VisitExpr(node.binding.init);
          }
          if (node.binding.pat) {
            VisitPattern(node.binding.pat);
          }
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          VisitExpr(node.place);
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          VisitExpr(node.place);
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
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
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        }
      },
      stmt);
}

void CaptureCollector::VisitBlock(const ast::Block& block) {
  locals.PushScope();
  for (const auto& stmt : block.stmts) {
    VisitStmt(stmt);
  }
  VisitExpr(block.tail_opt);
  locals.PopScope();
}

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
// LowerCtxSnapshot - saves and restores LowerCtx state
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
    // Merge derived_values: preserve new values created during nested lowering
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
// LowerDispatchExpr - Lower dispatch expression to IR
// =============================================================================
//
// Dispatch expression: dispatch pattern in range [opts] { body }
//
// Steps:
//   1. Lower range expression
//   2. Collect captures for body
//   3. Build capture environment tuple
//   4. Handle reduce options (Add, Mul, Min, Max, And, Or, Custom)
//   5. For custom reduce, generate reduce wrapper procedure
//   6. Generate body wrapper procedure with params: elem ptr, env ptr, result ptr, panic out
//   7. Create IRDispatch with pattern, range, body_fn, env, reduce info, chunk size, ordered flag

LowerResult LowerDispatchExpr(const ast::DispatchExpr& node, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Dispatch");

  // 1. Lower the range expression
  auto range_result = LowerExpr(*node.range, ctx);

  // 2. Collect captures for the body
  std::vector<CaptureBinding> captures;
  if (node.body) {
    captures = CollectCaptures(*node.body, ctx);
  }

  auto lowered_env = ctx.LowerParallelCaptureEnv(captures, "dispatch");
  auto env_parts = std::move(lowered_env.ir_parts);
  auto env_info = std::move(lowered_env.env_info);
  const analysis::TypeRef env_type = env_info.env_type;
  const IRValue env_ptr = env_info.env_param;

  // Determine element type (usize for dispatch iteration variable)
  analysis::TypeRef elem_type = analysis::MakeTypePrim("usize");

  // Determine body result type
  analysis::TypeRef body_type = analysis::MakeTypePrim("()");
  if (node.body && node.body->tail_opt && ctx.expr_type) {
    body_type = ctx.expr_type(*node.body->tail_opt);
  }
  if (!body_type) {
    body_type = analysis::MakeTypePrim("()");
  }

  // 4. Handle reduce options
  bool has_reduce = false;
  bool use_custom_reduce = false;
  std::optional<std::string> reduce_op;
  std::optional<std::string> custom_reduce_name;
  for (const auto& opt : node.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      has_reduce = true;
      switch (opt.reduce_op) {
        case ast::ReduceOp::Add: reduce_op = "+"; break;
        case ast::ReduceOp::Mul: reduce_op = "*"; break;
        case ast::ReduceOp::Min: reduce_op = "min"; break;
        case ast::ReduceOp::Max: reduce_op = "max"; break;
        case ast::ReduceOp::And: reduce_op = "and"; break;
        case ast::ReduceOp::Or: reduce_op = "or"; break;
        case ast::ReduceOp::Custom:
          use_custom_reduce = true;
          reduce_op = std::nullopt;
          custom_reduce_name = opt.custom_reduce_name;
          break;
      }
    }
  }

  // Compute element size for dispatch
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  std::uint64_t elem_size_val = 0;
  if (ctx.sigma) {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, elem_type)) {
      elem_size_val = *size;
    } else {
      ctx.ReportCodegenFailure();
    }
  }
  IRValue elem_size = USizeImmediate(elem_size_val, ctx);

  // Compute result size for reduce
  std::uint64_t result_size_val = 0;
  if (has_reduce && ctx.sigma) {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, body_type)) {
      result_size_val = *size;
    } else {
      ctx.ReportCodegenFailure();
    }
  }
  IRValue result_size = USizeImmediate(result_size_val, ctx);

  // Create result pointer
  IRValue result_ptr;
  if (has_reduce) {
    IRValue uninit = ctx.FreshTempValue("dispatch_result_init");
    const std::string result_name =
        ctx.FreshTempValue("dispatch_result_var").name;
    IRBindVar bind_result;
    bind_result.name = result_name;
    bind_result.value = uninit;
    bind_result.type = body_type;
    ctx.RegisterVar(result_name, body_type, false, false);
    bind_result.stable_name = ctx.StableBindingName(result_name);
    env_parts.push_back(MakeIR(std::move(bind_result)));

    DerivedValueInfo res_addr;
    res_addr.kind = DerivedValueInfo::Kind::AddrLocal;
    res_addr.name = result_name;
    result_ptr = ctx.FreshTempValue("dispatch_result_ptr");
    ctx.RegisterDerivedValue(result_ptr, res_addr);
    auto res_ptr_type =
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, body_type);
    ctx.RegisterValueType(result_ptr, res_ptr_type);
  } else {
    auto res_ptr_type =
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut,
                                 analysis::MakeTypePrim("u8"));
    result_ptr = MakeNullPtr(res_ptr_type, ctx, env_parts);
  }

  // 5. For custom reduce, generate reduce wrapper procedure
  std::optional<IRValue> reduce_fn;
  if (use_custom_reduce) {
    std::string wrapper_sym = NextDispatchSynthSymbol(ctx, "reduce");
    ProcIR proc;
    proc.symbol = wrapper_sym;
    proc.abi = std::string("C");
    proc.ret = analysis::MakeTypePrim("()");

    proc.params.push_back(HostedEnvParam());
    ctx.hosted_explicit_env_procs.insert(wrapper_sym);

    analysis::TypeRef lhs_ptr =
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Imm, body_type);
    analysis::TypeRef rhs_ptr =
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Imm, body_type);
    analysis::TypeRef out_ptr =
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, body_type);

    IRParam lhs_param;
    lhs_param.mode = analysis::ParamMode::Move;
    lhs_param.name = "lhs";
    lhs_param.type = lhs_ptr;
    proc.params.push_back(lhs_param);

    IRParam rhs_param;
    rhs_param.mode = analysis::ParamMode::Move;
    rhs_param.name = "rhs";
    rhs_param.type = rhs_ptr;
    proc.params.push_back(rhs_param);

    IRParam out_param;
    out_param.mode = analysis::ParamMode::Move;
    out_param.name = "out";
    out_param.type = out_ptr;
    proc.params.push_back(out_param);

    IRParam panic_param = PanicOutParam();
    proc.params.push_back(panic_param);

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
      ctx.RegisterVar(lhs_param.name, lhs_param.type, false, false);
      ctx.RegisterVar(rhs_param.name, rhs_param.type, false, false);
      ctx.RegisterVar(out_param.name, out_param.type, false, false);
      ctx.RegisterVar(panic_param.name, panic_param.type, true, false);

      IRValue lhs_val = ctx.FreshTempValue("reduce_lhs");
      IRValue rhs_val = ctx.FreshTempValue("reduce_rhs");
      IRValue lhs_ptr_val;
      lhs_ptr_val.kind = IRValue::Kind::Local;
      lhs_ptr_val.name = lhs_param.name;
      IRReadPtr read_lhs;
      read_lhs.ptr = lhs_ptr_val;
      read_lhs.result = lhs_val;
      IRValue rhs_ptr_val;
      rhs_ptr_val.kind = IRValue::Kind::Local;
      rhs_ptr_val.name = rhs_param.name;
      IRReadPtr read_rhs;
      read_rhs.ptr = rhs_ptr_val;
      read_rhs.result = rhs_val;

      ctx.RegisterValueType(lhs_val, body_type);
      ctx.RegisterValueType(rhs_val, body_type);

      std::vector<IRPtr> parts;
      parts.push_back(MakeIR(std::move(read_lhs)));
      parts.push_back(MakeIR(std::move(read_rhs)));

      IRValue callee;
      callee.kind = IRValue::Kind::Symbol;
      if (custom_reduce_name.has_value()) {
        callee.name = *custom_reduce_name;
        if (ctx.resolve_name) {
          if (const auto resolved = ctx.resolve_name(*custom_reduce_name)) {
            std::vector<std::string> full = *resolved;
            const std::string resolved_name = full.back();
            full.pop_back();
            IRReadPath read_path;
            read_path.path = std::move(full);
            read_path.name = resolved_name;
            parts.push_back(MakeIR(std::move(read_path)));
            callee.name = resolved_name;
          } else {
            ctx.ReportResolveFailure(*custom_reduce_name);
          }
        } else {
          ctx.ReportResolveFailure(*custom_reduce_name);
        }
      }

      IRValue panic_out_val;
      panic_out_val.kind = IRValue::Kind::Local;
      panic_out_val.name = panic_param.name;

      IRCall call_reduce;
      call_reduce.callee = callee;
      call_reduce.args = {lhs_val, rhs_val, panic_out_val};
      call_reduce.result = ctx.FreshTempValue("reduce_val");
      ctx.RegisterValueType(call_reduce.result, body_type);
      IRValue reduce_result_val = call_reduce.result;
      parts.push_back(MakeIR(std::move(call_reduce)));
      parts.push_back(PanicFollowup(ctx));

      IRValue out_ptr_val;
      out_ptr_val.kind = IRValue::Kind::Local;
      out_ptr_val.name = out_param.name;
      IRWritePtr write;
      write.ptr = out_ptr_val;
      write.value = reduce_result_val;
      parts.push_back(MakeIR(std::move(write)));

      CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
      IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
      if (cleanup_ir && !std::holds_alternative<IROpaque>(cleanup_ir->node)) {
        parts.push_back(cleanup_ir);
      }
      ctx.PopScope();

      IRValue unit_ret = ctx.FreshTempValue("unit");
      IRReturn ret;
      ret.value = unit_ret;
      parts.push_back(MakeIR(std::move(ret)));
      proc.body = SeqIR(std::move(parts));

      snapshot.Restore(ctx);
    }

    ctx.QueueExtraProc(std::move(proc), LinkageKind::Internal);

    IRValue fn_val;
    fn_val.kind = IRValue::Kind::Symbol;
    fn_val.name = wrapper_sym;
    reduce_fn = fn_val;
  }

  // 6. Generate body wrapper procedure
  std::string wrapper_sym = NextDispatchSynthSymbol(ctx, "body");
  ProcIR proc;
  proc.symbol = wrapper_sym;
  proc.abi = std::string("C");
  proc.ret = analysis::MakeTypePrim("()");

  proc.params.push_back(HostedEnvParam());
  ctx.hosted_explicit_env_procs.insert(wrapper_sym);

  analysis::TypeRef elem_ptr_type =
      analysis::MakeTypePtr(elem_type, analysis::PtrState::Valid);
  analysis::TypeRef env_ptr_type =
      analysis::MakeTypePtr(env_type, analysis::PtrState::Valid);
  analysis::TypeRef result_ptr_type =
      analysis::MakeTypePtr(body_type, analysis::PtrState::Valid);

  IRParam elem_param;
  elem_param.mode = analysis::ParamMode::Move;
  elem_param.name = "elem";
  elem_param.type = elem_ptr_type;
  proc.params.push_back(elem_param);

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
    ctx.RegisterVar(elem_param.name, elem_param.type, false, false);
    proc.params[1].stable_name = ctx.StableBindingName(elem_param.name);
    ctx.RegisterVar(env_param.name, env_param.type, false, false);
    proc.params[2].stable_name = ctx.StableBindingName(env_param.name);
    ctx.RegisterVar(result_param.name, result_param.type, false, false);
    proc.params[3].stable_name = ctx.StableBindingName(result_param.name);
    ctx.RegisterVar(panic_param.name, panic_param.type, true, false);
    proc.params[4].stable_name = ctx.StableBindingName(panic_param.name);

    IRPtr proc_region_ir = EnterSyntheticProcedureRegion(ctx);

    IRValue env_param_val;
    env_param_val.kind = IRValue::Kind::Local;
    env_param_val.name = env_param.name;
    ctx.BindAll(ctx.LoadEnv(env_param_val, env_info.env_type, env_info.captures));

    IRValue elem_ptr_val;
    elem_ptr_val.kind = IRValue::Kind::Local;
    elem_ptr_val.name = elem_param.name;
    IRValue elem_val = ctx.FreshTempValue("dispatch_elem");
    IRReadPtr read_elem;
    read_elem.ptr = elem_ptr_val;
    read_elem.result = elem_val;
    ctx.RegisterValueType(elem_val, elem_type);

    IRPtr bind_ir = EmptyIR();
    if (node.pattern) {
      RegisterPatternBindings(*node.pattern, elem_type, ctx);
      bind_ir = LowerBindPattern(*node.pattern, elem_val, ctx);
    }

    LowerResult body_result;
    if (node.body) {
      body_result = LowerBlock(*node.body, ctx);
    } else {
      IRValue unit_val = ctx.FreshTempValue("unit");
      body_result = LowerResult{EmptyIR(), unit_val};
    }
    ctx.active_region_aliases.pop_back();

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

    CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
    IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
    ctx.PopScope();

    IRValue unit_ret = ctx.FreshTempValue("unit");
    IRReturn ret;
    ret.value = unit_ret;

    std::vector<IRPtr> parts;
    parts.push_back(MakeIR(IRCancelSuppress{}));
    if (proc_region_ir && !std::holds_alternative<IROpaque>(proc_region_ir->node)) {
      parts.push_back(proc_region_ir);
    }
    parts.push_back(MakeIR(std::move(read_elem)));
    if (bind_ir && !std::holds_alternative<IROpaque>(bind_ir->node)) {
      parts.push_back(bind_ir);
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

  // 7. Create IRDispatch
  IRDispatch dispatch;
  dispatch.range = range_result.value;
  dispatch.body = EmptyIR();
  IRValue unit_body = ctx.FreshTempValue("unit");
  dispatch.body_result = unit_body;
  dispatch.captured_env = SeqIR(std::move(env_parts));
  dispatch.env_ptr = env_ptr;
  dispatch.body_fn.kind = IRValue::Kind::Symbol;
  dispatch.body_fn.name = wrapper_sym;
  dispatch.elem_size = elem_size;
  dispatch.result_size = result_size;
  dispatch.result_ptr = result_ptr;
  dispatch.reduce_op = reduce_op;
  dispatch.reduce_fn = reduce_fn;
  dispatch.result = ctx.FreshTempValue("dispatch_result");
  IRValue dispatch_result = dispatch.result;
  if (has_reduce) {
    ctx.RegisterValueType(dispatch_result, body_type);
  } else {
    ctx.RegisterValueType(dispatch_result, analysis::MakeTypePrim("()"));
  }

  // Handle chunk and ordered options
  IRPtr chunk_ir = EmptyIR();
  for (const auto& opt : node.opts) {
    if (opt.kind == ast::DispatchOptionKind::Ordered) {
      dispatch.ordered = true;
    } else if (opt.kind == ast::DispatchOptionKind::Chunk && opt.chunk_expr) {
      auto chunk_result = LowerExpr(*opt.chunk_expr, ctx);
      chunk_ir = chunk_result.ir;
      dispatch.chunk_size = chunk_result.value;
    }
  }

  std::vector<IRPtr> call_parts;
  call_parts.push_back(range_result.ir);
  if (chunk_ir && !std::holds_alternative<IROpaque>(chunk_ir->node)) {
    call_parts.push_back(chunk_ir);
  }
  call_parts.push_back(MakeIR(std::move(dispatch)));
  return LowerResult{SeqIR(std::move(call_parts)), dispatch_result};
}

}  // namespace ultraviolet::codegen
