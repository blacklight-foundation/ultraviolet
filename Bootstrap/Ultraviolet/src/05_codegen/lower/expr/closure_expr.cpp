// =============================================================================
// File: 05_codegen/lower/expr/closure_expr.cpp
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16260-16286 (Closure Lowering)
// =============================================================================
//
// Implements closure expression lowering per Section 6.4:
//   - Lower-Expr-Closure-NonCapturing: ClosureVal(null, sym)
//   - Lower-Expr-Closure-Capturing: Environment allocation + ClosureVal
//   - Lower-Closure-Call: IndirectCall with env_ptr
//
// =============================================================================

#include "05_codegen/lower/expr/closure_expr.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/symbols/mangle.h"
#include "05_codegen/cleanup/cleanup.h"
#include "04_analysis/typing/types.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

namespace {

constexpr std::string_view kClosureEnvParamName = "__env";

const ast::TypeAliasDecl* LookupCallableTypeAlias(
    const analysis::TypePath& path,
    const LowerCtx& ctx) {
  if (!ctx.sigma || path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(path));
    if (it == ctx.sigma->types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  ast::Path resolved;
  if (ctx.resolve_type_name) {
    if (auto resolved_path = ctx.resolve_type_name(path[0])) {
      resolved = *resolved_path;
    }
  }
  if (resolved.empty()) {
    resolved = ctx.module_path;
    resolved.push_back(path[0]);
  }

  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(resolved));
  if (it == ctx.sigma->types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&it->second);
}

// =============================================================================
// CaptureBinding - Information about a single captured variable
// =============================================================================

struct CaptureBinding {
  std::string name;
  analysis::TypeRef type;
  bool by_move = false;  // true for move captures, false for reference captures
  bool from_capture_env = false;
};

// =============================================================================
// ScopedNames - Tracks locally-defined names within nested scopes
// =============================================================================

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

// =============================================================================
// ClosureCaptureCollector - Collects captured variables from a closure body
// =============================================================================

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

struct ClosureCaptureCollector {
  LowerCtx& ctx;
  const std::unordered_set<std::string>& move_captures;
  std::unordered_map<std::string, CaptureBinding> captures;
  std::vector<std::string> order;
  ScopedNames locals;

  void RecordCapture(std::string_view name) {
    const std::string key(name);
    if (locals.IsLocal(key)) {
      return;
    }
    if (captures.find(key) != captures.end()) {
      return;
    }

    analysis::TypeRef cap_type;
    bool from_capture_env = false;
    if (const auto* binding = ctx.GetBindingState(key)) {
      cap_type = binding->type;
    } else if (const auto* capture = ctx.LookupCapture(key)) {
      cap_type = capture->value_type;
      from_capture_env = true;
    }
    if (!cap_type) {
      return;
    }

    CaptureBinding entry;
    entry.name = key;
    entry.type = cap_type;
    entry.by_move = move_captures.find(key) != move_captures.end();
    entry.from_capture_env = from_capture_env;
    captures.emplace(key, entry);
    order.push_back(key);
  }

  void VisitExpr(const ast::ExprPtr& expr);
  void VisitStmt(const ast::Stmt& stmt);
  void VisitBlock(const ast::Block& block);
};

void ClosureCaptureCollector::VisitExpr(const ast::ExprPtr& expr) {
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
          VisitExpr(node.then_expr);
          VisitExpr(node.else_expr);
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
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          VisitExpr(node.value);
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

void ClosureCaptureCollector::VisitStmt(const ast::Stmt& stmt) {
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
          VisitExpr(node.binding.init);
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

void ClosureCaptureCollector::VisitBlock(const ast::Block& block) {
  locals.Push();
  for (const auto& stmt : block.stmts) {
    VisitStmt(stmt);
  }
  VisitExpr(block.tail_opt);
  locals.Pop();
}

// =============================================================================
// CollectClosureCaptures - Collect the capture set for a closure body
// =============================================================================

std::vector<CaptureBinding> CollectClosureCaptures(
    const ast::Block& body,
    LowerCtx& ctx,
    const std::unordered_set<std::string>& move_captures) {
  ClosureCaptureCollector collector{ctx, move_captures, {}, {}, {}};
  collector.VisitBlock(body);
  std::sort(collector.order.begin(), collector.order.end());
  std::vector<CaptureBinding> result;
  result.reserve(collector.order.size());
  for (const auto& key : collector.order) {
    result.push_back(collector.captures.at(key));
  }
  return result;
}

// =============================================================================
// ClosureEnvLayout - Compute environment layout for captured variables
// =============================================================================

struct ClosureEnvLayout {
  std::uint64_t size = 0;
  std::uint64_t align = 1;
  std::vector<std::uint64_t> offsets;
};

ClosureEnvLayout ComputeClosureEnvLayout(
    const std::vector<CaptureBinding>& captures,
    const analysis::ScopeContext& scope) {
  ClosureEnvLayout layout;
  layout.offsets.reserve(captures.size());

  std::uint64_t offset = 0;
  std::uint64_t max_align = 1;

  for (const auto& cap : captures) {
    analysis::TypeRef field_type = cap.type;
    if (!cap.by_move) {
      // Reference captures use a pointer type
      field_type = analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid);
    }

    const auto field_layout = ::ultraviolet::analysis::layout::LayoutOf(scope, field_type);
    if (!field_layout) {
      continue;
    }

    // Align the offset
    const std::uint64_t field_align = field_layout->align;
    if (field_align > 0) {
      offset = (offset + field_align - 1) / field_align * field_align;
    }

    layout.offsets.push_back(offset);
    offset += field_layout->size;

    if (field_align > max_align) {
      max_align = field_align;
    }
  }

  // Final size with alignment padding
  if (max_align > 0) {
    layout.size = (offset + max_align - 1) / max_align * max_align;
  } else {
    layout.size = offset;
  }
  layout.align = max_align;

  return layout;
}

// =============================================================================
// ClosureCodeSym - Generate mangled symbol for closure code
// =============================================================================

std::string ClosureCodeSym(LowerCtx& ctx) {
  const std::uint64_t closure_index = ctx.current_closure_counter++;
  if (ctx.current_proc_symbol.has_value() && !ctx.current_proc_symbol->empty()) {
    return MangleClosure(*ctx.current_proc_symbol, closure_index);
  }
  if (!ctx.module_path.empty()) {
    return MangleClosure(ScopedSym(ctx.module_path), closure_index);
  }
  return MangleClosure("_module", closure_index);
}

// =============================================================================
// LowerCtxSnapshot - Save and restore LowerCtx state for nested lowering
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
        proc_ret_type(ctx.proc_ret_type) {}

  void Restore(LowerCtx& ctx) const {
    ctx.scope_stack = scope_stack;
    ctx.binding_states = binding_states;
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
  }
};

// =============================================================================
// Check if type is unit
// =============================================================================

bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

}  // namespace

// =============================================================================
// ClosureVal - Representation of a closure value (env_ptr, code_ptr)
// =============================================================================
//
// A ClosureVal consists of:
//   - env_ptr: Pointer to the captured environment (null for non-capturing)
//   - code_ptr: Symbol reference to the closure code procedure
//
// The closure code procedure has signature:
//   For non-capturing: (params...) -> ret
//   For capturing: (env_ptr, params...) -> ret
//
// =============================================================================

ClosureVal MakeClosureVal(const IRValue& env_ptr, const std::string& code_sym) {
  ClosureVal val;
  val.env_ptr = env_ptr;
  val.code_sym = code_sym;
  return val;
}

ClosureVal MakeNonCapturingClosureVal(const std::string& code_sym) {
  ClosureVal val;
  val.env_ptr.kind = IRValue::Kind::Opaque;
  val.env_ptr.name = "null";
  val.code_sym = code_sym;
  return val;
}

analysis::TypeRef NormalizeCallableAliasForLowering(const analysis::TypeRef& type,
                                                    const LowerCtx& ctx) {
  analysis::TypeRef current = analysis::StripPerm(type);
  if (!current) {
    current = type;
  }

  for (std::uint32_t depth = 0; depth < 32; ++depth) {
    if (!current) {
      return type;
    }
    if (std::holds_alternative<analysis::TypeFunc>(current->node) ||
        std::holds_alternative<analysis::TypeClosure>(current->node)) {
      return current;
    }

    const auto* path = analysis::AppliedTypePath(*current);
    const auto* args = analysis::AppliedTypeArgs(*current);
    if (!path || !args) {
      return current;
    }

    const auto* alias = LookupCallableTypeAlias(*path, ctx);
    if (!alias) {
      return current;
    }

    const auto lowered = analysis::LowerType(ScopeForLowering(ctx), alias->type);
    if (!lowered.ok || !lowered.type) {
      return current;
    }

    analysis::TypeRef expanded = lowered.type;
    if (alias->generic_params.has_value()) {
      const auto& params = alias->generic_params->params;
      if (args->size() > params.size()) {
        return current;
      }
      const auto subst = analysis::BuildSubstitution(params, *args);
      expanded = analysis::InstantiateType(expanded, subst);
    } else if (!args->empty()) {
      return current;
    }

    current = analysis::StripPerm(expanded);
    if (!current) {
      current = expanded;
    }
  }

  return current ? current : type;
}

// =============================================================================
// LowerClosureExpr - Lower a closure expression
// =============================================================================
//
// SPEC_RULE("Lower-Expr-Closure-NonCapturing"):
//   If CaptureSet is empty, return ClosureVal(null, sym)
//
// SPEC_RULE("Lower-Expr-Closure-Capturing"):
//   - Get closure code symbol
//   - Compute closure environment layout (size, align, offsets)
//   - Lower capture environment: allocate env, store each capture
//   - Return ClosureVal(env_ptr, sym)
//
// =============================================================================

LowerResult LowerClosureExpr(
    const std::vector<ast::Param>& params,
    const ast::TypePtr& ret_type_opt,
    const ast::Block& body,
    const std::unordered_set<std::string>& move_captures,
    LowerCtx& ctx,
    const std::vector<analysis::TypeRef>* inferred_param_types,
    const std::vector<std::optional<analysis::ParamMode>>* inferred_param_modes,
    analysis::TypeRef inferred_ret_type) {

  // Step 1: Collect captures from the closure body
  std::vector<CaptureBinding> captures = CollectClosureCaptures(body, ctx, move_captures);

  // Step 2: Generate closure code symbol
  std::string code_sym = ClosureCodeSym(ctx);

  // Step 3: Determine return type
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  analysis::TypeRef ret_type = nullptr;
  if (ret_type_opt) {
    if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, ret_type_opt)) {
      ret_type = *lowered;
    } else {
      ctx.ReportCodegenFailure();
    }
  }
  if (!ret_type && inferred_ret_type) {
    ret_type = inferred_ret_type;
  }
  if (!ret_type && body.tail_opt && ctx.expr_type) {
    ret_type = ctx.expr_type(*body.tail_opt);
  }
  if (!ret_type) {
    ret_type = analysis::MakeTypePrim("()");
  }

  // Step 4: Check if non-capturing
  if (captures.empty()) {
    SPEC_RULE("Lower-Expr-Closure-NonCapturing");

    // Generate the closure code procedure.
    // Non-capturing closures type as TypeFunc, so the emitted callable must
    // use the ordinary function ABI with no hidden env parameter.
    ProcIR proc;
    proc.symbol = code_sym;
    proc.ret = ret_type;

    // Add parameters from closure signature
    std::vector<analysis::TypeFuncParam> fn_type_params;
    fn_type_params.reserve(params.size());
    for (std::size_t param_index = 0; param_index < params.size(); ++param_index) {
      const auto& param = params[param_index];
      IRParam ir_param;
      analysis::TypeFuncParam fn_param;
      if (param.mode.has_value()) {
        ir_param.mode = analysis::ParamMode::Move;
        fn_param.mode = analysis::ParamMode::Move;
      } else if (inferred_param_modes &&
                 param_index < inferred_param_modes->size() &&
                 (*inferred_param_modes)[param_index].has_value()) {
        ir_param.mode = (*inferred_param_modes)[param_index];
        fn_param.mode = (*inferred_param_modes)[param_index];
      }
      ir_param.name = param.name;
      analysis::TypeRef param_type;
      if (param.type && ctx.sigma) {
        if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, param.type)) {
          param_type = *lowered;
        }
      }
      if (!param_type && inferred_param_types &&
          param_index < inferred_param_types->size()) {
        param_type = (*inferred_param_types)[param_index];
      }
      if (!param_type) {
        ctx.ReportCodegenFailure();
        param_type = analysis::MakeTypePrim("()");
      }
      ir_param.type = param_type;
      fn_param.type = param_type;
      proc.params.push_back(ir_param);
      fn_type_params.push_back(std::move(fn_param));
    }

    proc.params.push_back(PanicOutParam());

    // Lower the body
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
      ctx.proc_ret_type = ret_type;

      ctx.PushScope(false, false);
      for (auto& param : proc.params) {
        ctx.RegisterVar(param.name, param.type, true, false);
        param.stable_name = ctx.StableBindingName(param.name);
      }

      LowerResult body_result = LowerBlock(body, ctx);

      analysis::TypeRef effective_ret = ret_type;
      if (!effective_ret) {
        if (analysis::TypeRef inferred = ctx.LookupValueType(body_result.value)) {
          effective_ret = inferred;
        } else if (body.tail_opt && ctx.expr_type) {
          effective_ret = ctx.expr_type(*body.tail_opt);
        }
      }
      if (!effective_ret) {
        effective_ret = analysis::MakeTypePrim("()");
      }
      proc.ret = effective_ret;

      // Generate return
      std::vector<IRPtr> parts;
      if (body_result.ir) {
        parts.push_back(body_result.ir);
      }

      if (!IsUnitType(effective_ret)) {
        IRReturn ret;
        ret.value = body_result.value;
        parts.push_back(MakeIR(std::move(ret)));
      } else {
        IRValue unit_ret = ctx.FreshTempValue("unit");
        IRReturn ret;
        ret.value = unit_ret;
        parts.push_back(MakeIR(std::move(ret)));
      }

      proc.body = SeqIR(std::move(parts));

      ctx.PopScope();
      snapshot.Restore(ctx);
    }

    analysis::TypeRef result_ret_type = proc.ret;
    ctx.QueueExtraProc(std::move(proc), LinkageKind::Internal);

    IRValue result;
    result.kind = IRValue::Kind::Symbol;
    result.name = code_sym;
    ctx.RegisterValueType(
        result,
        analysis::MakeTypeFunc(std::move(fn_type_params), result_ret_type));

    return LowerResult{EmptyIR(), result};
  }

  // Step 5: Capturing closure
  SPEC_RULE("Lower-Expr-Closure-Capturing");

  // Compute environment layout
  ClosureEnvLayout env_layout = ComputeClosureEnvLayout(captures, scope);

  // Build environment type
  std::vector<analysis::TypeRef> env_fields;
  for (const auto& cap : captures) {
    if (cap.by_move || cap.from_capture_env) {
      env_fields.push_back(cap.type);
    } else {
      env_fields.push_back(analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid));
    }
  }
  analysis::TypeRef env_type = analysis::MakeTypeTuple(env_fields);

  // Step 6: Lower capture environment
  std::vector<IRPtr> env_parts;
  std::vector<std::string> move_capture_names;

  CaptureEnvInfo env_info;
  env_info.env_type = env_type;

  // Materialize the environment through an explicit allocation so closure
  // lowering follows the spec's LowerCaptureEnv/StoreCapture shape rather than
  // relying on a local tuple temporary whose address is taken afterwards.
  IRValue env_ptr = ctx.FreshTempValue("closure_env_ptr");
  ctx.RegisterValueType(
      env_ptr,
      analysis::MakeTypePtr(env_type, analysis::PtrState::Valid));

  IRValue env_zero = ctx.FreshTempValue("closure_env_zero");
  ctx.RegisterValueType(env_zero, env_type);

  if (!ctx.active_region_aliases.empty()) {
    IRAlloc alloc_env;
    alloc_env.value = env_zero;
    alloc_env.result = env_ptr;
    alloc_env.type = env_type;
    IRValue region_local;
    region_local.kind = IRValue::Kind::Local;
    region_local.name = ctx.active_region_aliases.back();
    alloc_env.region = region_local;
    env_parts.push_back(MakeIR(std::move(alloc_env)));
  } else {
    IRValue env_storage;
    env_storage.kind = IRValue::Kind::Local;
    env_storage.name = ctx.FreshTempValue("closure_env_storage").name;
    ctx.RegisterValueType(env_storage, env_type);

    IRBindVar bind_env;
    bind_env.name = env_storage.name;
    bind_env.value = env_zero;
    bind_env.type = env_type;
    env_parts.push_back(MakeIR(std::move(bind_env)));

    DerivedValueInfo env_addr;
    env_addr.kind = DerivedValueInfo::Kind::AddrLocal;
    env_addr.name = env_storage.name;
    ctx.RegisterDerivedValue(env_ptr, env_addr);
  }

  for (std::size_t i = 0; i < captures.size(); ++i) {
    const auto& cap = captures[i];
    if (cap.by_move) {
      move_capture_names.push_back(cap.name);
    }

    CaptureAccess access;
    access.index = i;
    if (i < env_layout.offsets.size()) {
      access.byte_offset = env_layout.offsets[i];
    }
    access.value_type = cap.type;
    access.by_ref = !cap.by_move && !cap.from_capture_env;

    if (cap.by_move || cap.from_capture_env) {
      access.field_type = cap.type;
    } else {
      access.field_type = analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid);
    }
    env_info.captures[cap.name] = access;

    IRValue field_val;
    IRPtr field_ir = EmptyIR();
    ast::Expr ident_expr;
    ident_expr.node = ast::IdentifierExpr{cap.name};

    if (cap.by_move) {
      // Move capture: move the value, including values sourced from an
      // enclosing capture environment.
      auto move_res = LowerMovePlace(ident_expr, ctx);
      field_val = move_res.value;
      field_ir = move_res.ir;
    } else if (!cap.from_capture_env) {
      // Reference capture: store address of variable
      IRValue ptr = ctx.FreshTempValue("capture_addr");
      DerivedValueInfo addr_info;
      addr_info.kind = DerivedValueInfo::Kind::AddrLocal;
      addr_info.name = cap.name;
      ctx.RegisterDerivedValue(ptr, addr_info);
      ctx.RegisterValueType(
          ptr,
          analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid));
      field_val = ptr;
    } else {
      // Captured name already comes from an enclosing capture environment;
      // materialize the current value and store it directly.
      auto read_res = LowerExpr(ident_expr, ctx);
      field_ir = read_res.ir;
      field_val = read_res.value;
    }

    if (field_ir && !std::holds_alternative<IROpaque>(field_ir->node)) {
      env_parts.push_back(field_ir);
    }

    IRValue field_ptr = ctx.FreshTempValue("closure_field_ptr");
    DerivedValueInfo field_info;
    field_info.kind = DerivedValueInfo::Kind::AddrTuple;
    field_info.base = env_ptr;
    field_info.tuple_index = i;
    field_info.byte_offset = access.byte_offset;
    ctx.RegisterDerivedValue(field_ptr, field_info);
    ctx.RegisterValueType(
        field_ptr,
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, access.field_type));

    IRWritePtr store_field;
    store_field.ptr = field_ptr;
    store_field.value = field_val;
    env_parts.push_back(MakeIR(std::move(store_field)));
  }

  // Mirror the spec-owned MarkMoved(MoveCaptureSet(C)) helper surface at the
  // closure-construction boundary. Local move captures were already marked by
  // LowerMovePlace; repeated marking is idempotent, while captured bindings
  // from an enclosing environment are skipped here.
  ctx.MarkMoved(move_capture_names);

  env_info.env_param = env_ptr;

  // Step 7: Generate closure code procedure (with env parameter)
  ProcIR proc;
  proc.symbol = code_sym;
  proc.ret = ret_type;

  // First parameter is env pointer
  IRParam env_param;
  env_param.mode = analysis::ParamMode::Move;
  env_param.name = std::string(kClosureEnvParamName);
  env_param.type =
      analysis::MakeTypeRawPtr(analysis::RawPtrQual::Imm,
                               analysis::MakeTypePrim("u8"));
  proc.params.push_back(env_param);

  // Add parameters from closure signature
  for (std::size_t param_index = 0; param_index < params.size(); ++param_index) {
    const auto& param = params[param_index];
    IRParam ir_param;
    if (param.mode.has_value()) {
      ir_param.mode = analysis::ParamMode::Move;
    } else if (inferred_param_modes &&
               param_index < inferred_param_modes->size() &&
               (*inferred_param_modes)[param_index].has_value()) {
      ir_param.mode = (*inferred_param_modes)[param_index];
    }
    ir_param.name = param.name;
    analysis::TypeRef param_type;
    if (param.type && ctx.sigma) {
      if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, param.type)) {
        param_type = *lowered;
      }
    }
    if (!param_type && inferred_param_types &&
        param_index < inferred_param_types->size()) {
      param_type = (*inferred_param_types)[param_index];
    }
    if (!param_type) {
      ctx.ReportCodegenFailure();
      param_type = analysis::MakeTypePrim("()");
    }
    ir_param.type = param_type;
    proc.params.push_back(ir_param);
  }

  proc.params.push_back(PanicOutParam());

  // Lower the body with capture environment available
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
    ctx.proc_ret_type = ret_type;

    ctx.PushScope(false, false);
    for (auto& param : proc.params) {
      ctx.RegisterVar(param.name, param.type, true, false);
      param.stable_name = ctx.StableBindingName(param.name);
    }
    // Closure code signature uses raw `*imm u8` for ABI conformance.
    // Internally, treat the env param as a raw pointer to the synthesized
    // env tuple so captured-field addressing can use tuple layout metadata.
    {
      const auto it = ctx.binding_states.find(std::string(kClosureEnvParamName));
      if (it != ctx.binding_states.end() && !it->second.empty()) {
        it->second.back().type =
            analysis::MakeTypePtr(env_type, analysis::PtrState::Valid);
      }
      IRValue env_local;
      env_local.kind = IRValue::Kind::Local;
      env_local.name = std::string(kClosureEnvParamName);
      ctx.RegisterValueType(
          env_local,
          analysis::MakeTypePtr(env_type, analysis::PtrState::Valid));
    }

    // Set up capture environment for body lowering
    IRValue env_param_val;
    env_param_val.kind = IRValue::Kind::Local;
    env_param_val.name = std::string(kClosureEnvParamName);
    ctx.BindAll(ctx.LoadEnv(env_param_val, env_info.env_type, env_info.captures));

    LowerResult body_result = LowerBlock(body, ctx);

    analysis::TypeRef effective_ret = ret_type;
    if (!effective_ret) {
      if (analysis::TypeRef inferred = ctx.LookupValueType(body_result.value)) {
        effective_ret = inferred;
      } else if (body.tail_opt && ctx.expr_type) {
        effective_ret = ctx.expr_type(*body.tail_opt);
      }
    }
    if (!effective_ret) {
      effective_ret = analysis::MakeTypePrim("()");
    }
    proc.ret = effective_ret;

    // Generate cleanup and return
    CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
    IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);

    std::vector<IRPtr> parts;
    if (body_result.ir) {
      parts.push_back(body_result.ir);
    }
    if (cleanup_ir && !std::holds_alternative<IROpaque>(cleanup_ir->node)) {
      parts.push_back(cleanup_ir);
    }

    if (!IsUnitType(effective_ret)) {
      IRReturn ret;
      ret.value = body_result.value;
      parts.push_back(MakeIR(std::move(ret)));
    } else {
      IRValue unit_ret = ctx.FreshTempValue("unit");
      IRReturn ret;
      ret.value = unit_ret;
      parts.push_back(MakeIR(std::move(ret)));
    }

    proc.body = SeqIR(std::move(parts));

    ctx.PopScope();
    snapshot.Restore(ctx);
  }

  ctx.QueueExtraProc(std::move(proc), LinkageKind::Internal);

  // Step 8: Return closure value
  ClosureVal closure = MakeClosureVal(env_ptr, code_sym);
  IRValue result = ctx.FreshTempValue("closure");

  // Register the closure value representation
  DerivedValueInfo closure_info;
  closure_info.kind = DerivedValueInfo::Kind::TupleLit;
  closure_info.elements.push_back(closure.env_ptr);
  IRValue code_val;
  code_val.kind = IRValue::Kind::Symbol;
  code_val.name = code_sym;
  closure_info.elements.push_back(code_val);
  ctx.RegisterDerivedValue(result, closure_info);

  return LowerResult{SeqIR(std::move(env_parts)), result};
}

// =============================================================================
// LowerClosureCall - Lower a call to a closure value
// =============================================================================
//
// SPEC_RULE("Lower-Closure-Call"):
//   - Extract env_ptr and code_ptr from closure value
//   - Lower arguments
//   - Create IndirectCall(code_ptr, [env_ptr] ++ args)
//
// =============================================================================

LowerResult LowerClosureCall(
    const ast::Expr& closure_expr,
    const std::vector<ast::Arg>& args,
    LowerCtx& ctx) {
  SPEC_RULE("Lower-Closure-Call");

  // Step 1: Lower the closure expression to get closure value
  LowerResult closure_result = LowerExpr(closure_expr, ctx);

  // Step 2: Extract env_ptr and code_ptr from closure value
  // The closure value is a tuple (env_ptr, code_ptr)
  analysis::TypeRef closure_result_type = nullptr;
  analysis::TypeRef closure_code_type = nullptr;
  ParamModeList param_modes;
  ParamTypeList param_types;
  if (ctx.expr_type) {
    if (analysis::TypeRef callee_type = ctx.expr_type(closure_expr)) {
      analysis::TypeRef callable_type =
          NormalizeCallableAliasForLowering(callee_type, ctx);
      if (analysis::TypeRef func_type = GetClosureFuncType(callable_type)) {
        if (const auto* fn = std::get_if<analysis::TypeFunc>(&func_type->node)) {
          closure_result_type = fn->ret;
          param_modes.reserve(fn->params.size());
          param_types.reserve(fn->params.size());
          for (const auto& param : fn->params) {
            param_modes.push_back(param.mode);
            param_types.push_back(param.type);
          }
        }
      }
      if (const auto* closure =
              callable_type ? std::get_if<analysis::TypeClosure>(&callable_type->node)
                            : nullptr) {
        std::vector<analysis::TypeFuncParam> code_params;
        analysis::TypeFuncParam env_param;
        env_param.mode = analysis::ParamMode::Move;
        env_param.type = analysis::MakeTypeRawPtr(
            analysis::RawPtrQual::Imm,
            analysis::MakeTypePrim("u8"));
        code_params.push_back(std::move(env_param));
        for (const auto& [is_move, param_type] : closure->params) {
          analysis::TypeFuncParam code_param;
          if (is_move) {
            code_param.mode = analysis::ParamMode::Move;
          }
          code_param.type = param_type;
          code_params.push_back(std::move(code_param));
        }
        closure_code_type =
            analysis::MakeTypeFunc(std::move(code_params), closure->ret);
      }
    }
  }

  IRValue env_ptr = ctx.FreshTempValue("closure_env");
  DerivedValueInfo env_info;
  env_info.kind = DerivedValueInfo::Kind::Tuple;
  env_info.base = closure_result.value;
  env_info.tuple_index = 0;
  ctx.RegisterDerivedValue(env_ptr, env_info);

  IRValue code_ptr = ctx.FreshTempValue("closure_code");
  DerivedValueInfo code_info;
  code_info.kind = DerivedValueInfo::Kind::Tuple;
  code_info.base = closure_result.value;
  code_info.tuple_index = 1;
  ctx.RegisterDerivedValue(code_ptr, code_info);
  if (closure_code_type) {
    ctx.RegisterValueType(code_ptr, closure_code_type);
  }

  // Step 3: Lower arguments with the closure parameter modes.
  IRPtr args_ir = EmptyIR();
  std::vector<IRValue> arg_values;
  if (param_modes.size() == args.size()) {
    auto lowered_args = LowerArgs(
        param_modes,
        args,
        ctx,
        param_types.size() == args.size() ? &param_types : nullptr);
    args_ir = lowered_args.first;
    arg_values = std::move(lowered_args.second);
  } else {
    ctx.ReportCodegenFailure();
    std::vector<IRPtr> arg_parts;
    for (const auto& arg : args) {
      if (arg.value) {
        LowerResult arg_result = LowerExpr(*arg.value, ctx);
        if (arg_result.ir && !std::holds_alternative<IROpaque>(arg_result.ir->node)) {
          arg_parts.push_back(arg_result.ir);
        }
        arg_values.push_back(arg_result.value);
      }
    }
    args_ir = SeqIR(std::move(arg_parts));
  }

  // Step 4: Build argument list: [env_ptr] ++ args
  std::vector<IRValue> call_args;
  call_args.push_back(env_ptr);
  for (const auto& arg_val : arg_values) {
    call_args.push_back(arg_val);
  }
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  call_args.push_back(panic_out);

  // Step 5: Create indirect call
  IRCall call;
  call.callee = code_ptr;
  call.args = call_args;
  call.result = ctx.FreshTempValue("closure_call_result");
  const IRValue closure_call_result = call.result;
  if (closure_result_type) {
    ctx.RegisterValueType(call.result, closure_result_type);
  }

  // Combine all IR parts
  std::vector<IRPtr> parts;
  if (closure_result.ir && !std::holds_alternative<IROpaque>(closure_result.ir->node)) {
    parts.push_back(closure_result.ir);
  }
  if (args_ir && !std::holds_alternative<IROpaque>(args_ir->node)) {
    parts.push_back(args_ir);
  }
  parts.push_back(MakeIR(std::move(call)));
  parts.push_back(PanicFollowup(ctx));

  if (closure_result_type) {
    IRValue closure_call_value;
    closure_call_value.kind = IRValue::Kind::Local;
    closure_call_value.name = ctx.FreshTempValue("closure_call_value").name;
    ctx.RegisterValueType(closure_call_value, closure_result_type);

    IRBindVar bind_result;
    bind_result.name = closure_call_value.name;
    bind_result.value = closure_call_result;
    bind_result.type = closure_result_type;
    parts.push_back(MakeIR(std::move(bind_result)));

    return LowerResult{SeqIR(std::move(parts)), closure_call_value};
  }

  return LowerResult{SeqIR(std::move(parts)), closure_call_result};
}

// =============================================================================
// IsClosureType - Check if a type is a closure type
// =============================================================================

bool IsClosureType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  analysis::TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    break;
  }
  if (stripped && std::holds_alternative<analysis::TypeClosure>(stripped->node)) {
    return true;
  }
  return false;
}

// =============================================================================
// GetClosureFuncType - Extract the function type from a closure type
// =============================================================================

analysis::TypeRef GetClosureFuncType(const analysis::TypeRef& closure_type) {
  if (!closure_type) {
    return nullptr;
  }
  analysis::TypeRef stripped = closure_type;
  while (stripped) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    break;
  }
  if (stripped && std::holds_alternative<analysis::TypeFunc>(stripped->node)) {
    return stripped;
  }
  if (const auto* closure = std::get_if<analysis::TypeClosure>(&stripped->node)) {
    std::vector<analysis::TypeFuncParam> params;
    params.reserve(closure->params.size());
    for (const auto& [is_move, param_type] : closure->params) {
      analysis::TypeFuncParam p;
      if (is_move) {
        p.mode = analysis::ParamMode::Move;
      } else {
        p.mode = std::nullopt;
      }
      p.type = param_type;
      params.push_back(std::move(p));
    }
    return analysis::MakeTypeFunc(std::move(params), closure->ret);
  }
  return nullptr;
}

namespace {

LowerResult LowerClosureExprFromNode(
    const ast::ClosureExpr& expr,
    LowerCtx& ctx,
    const std::vector<analysis::TypeRef>* inferred_param_types,
    const std::vector<std::optional<analysis::ParamMode>>* inferred_param_modes,
    analysis::TypeRef inferred_ret_type) {
  // Convert ClosureExpr to the decomposed form
  // ClosureParam has: move_capture, name, type_opt
  // We need to convert to vector<ast::Param>
  std::vector<ast::Param> params;
  std::unordered_set<std::string> move_captures;

  for (const auto& cp : expr.params) {
    ast::Param param;
    if (cp.move_capture) {
      param.mode = ast::ParamMode::Move;
      move_captures.insert(cp.name);
    }
    param.name = cp.name;
    param.type = cp.type_opt;
    params.push_back(std::move(param));
  }

  // The body is an ExprPtr - we need to handle it as a Block
  // If body is a BlockExpr, extract the block; otherwise wrap it
  if (!expr.body) {
    ctx.ReportCodegenFailure();
    return LowerResult{EmptyIR(), ctx.FreshTempValue("closure_err")};
  }

  // Check if body is a BlockExpr
  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&expr.body->node)) {
    if (block_expr->block) {
      return LowerClosureExpr(params, expr.ret_type_opt, *block_expr->block,
                              move_captures, ctx, inferred_param_types,
                              inferred_param_modes, inferred_ret_type);
    }
  }

  // Otherwise, create a synthetic block with the expression as tail
  ast::Block synth_block;
  synth_block.tail_opt = expr.body;

  return LowerClosureExpr(params, expr.ret_type_opt, synth_block, move_captures,
                          ctx, inferred_param_types, inferred_param_modes,
                          inferred_ret_type);
}

}  // namespace

// =============================================================================
// LowerClosureExpr - Overload accepting ast::ClosureExpr directly
// =============================================================================

LowerResult LowerClosureExpr(const ast::ClosureExpr& expr, LowerCtx& ctx) {
  return LowerClosureExprFromNode(expr, ctx, nullptr, nullptr, nullptr);
}

LowerResult LowerClosureExpr(const ast::Expr& expr,
                             const ast::ClosureExpr& closure,
                             LowerCtx& ctx) {
  std::vector<analysis::TypeRef> inferred_param_types;
  std::vector<std::optional<analysis::ParamMode>> inferred_param_modes;
  analysis::TypeRef inferred_ret_type = nullptr;

  if (ctx.expr_type) {
    if (analysis::TypeRef closure_type = ctx.expr_type(expr)) {
      analysis::TypeRef callable_type =
          NormalizeCallableAliasForLowering(closure_type, ctx);
      if (analysis::TypeRef func_type = GetClosureFuncType(callable_type)) {
        if (const auto* fn = std::get_if<analysis::TypeFunc>(&func_type->node)) {
          inferred_ret_type = fn->ret;
          inferred_param_types.reserve(fn->params.size());
          inferred_param_modes.reserve(fn->params.size());
          for (const auto& fn_param : fn->params) {
            inferred_param_types.push_back(fn_param.type);
            inferred_param_modes.push_back(fn_param.mode);
          }
        }
      }
    }
  }

  const auto* inferred_types_ptr =
      inferred_param_types.empty() ? nullptr : &inferred_param_types;
  const auto* inferred_modes_ptr =
      inferred_param_modes.empty() ? nullptr : &inferred_param_modes;

  return LowerClosureExprFromNode(closure, ctx, inferred_types_ptr,
                                  inferred_modes_ptr, inferred_ret_type);
}

}  // namespace ultraviolet::codegen
