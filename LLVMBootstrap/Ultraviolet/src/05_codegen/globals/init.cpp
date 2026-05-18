// =============================================================================
// MIGRATION MAPPING: init.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.0 CG-Module rule (lines 14228-14231)
//   - InitFn(m), DeinitFn(m) (line 14229)
//   - Lower-StaticInit, Lower-StaticDeinit (line 14229)
//   - Module initialization ordering
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/init.cpp
//   - Module init/deinit function generation
//   - Static initialization code emission
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/globals/init.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h (ProcIR)
//   - ultraviolet/include/05_codegen/symbols/mangle.h (module symbols)
//
// REFACTORING NOTES:
//   1. Each module has init and deinit functions
//   2. InitFn handles:
//      - Static variable initialization
//      - Module-level setup
//   3. DeinitFn handles:
//      - Static variable cleanup (Drop)
//      - Module-level teardown
//   4. Functions take PanicOutParam for error handling
//   5. Called by runtime in dependency order
//   6. EmitInitPlan generates full program init sequence
//
// MODULE LIFECYCLE:
//   1. Runtime loads all modules
//   2. Calls InitFn for each in dependency order
//   3. ... program execution ...
//   4. Calls DeinitFn in reverse order
//
// INIT FUNCTION STRUCTURE:
//   proc __module_init(__panic: *mut PanicRecord) -> () {
//     // Initialize statics in declaration order
//     // Call dependency module inits
//   }
// =============================================================================

#include "05_codegen/globals/init.h"

#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "05_codegen/globals/globals.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/drop_hooks.h"
#include "05_codegen/common/runtime_trace_utils.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_pat.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"
#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"

namespace ultraviolet::codegen {

namespace {

std::string ModulePathString(const ast::ModulePath& path) {
  std::string out;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i) {
      out += "::";
    }
    out += path[i];
  }
  return out;
}

std::string InitItemSym(const ast::ModulePath& module_path, std::size_t index) {
  std::vector<std::string> path = {
      std::string(project::ActiveLanguageProfile().runtime_root),
      "runtime",
      "init_item"};
  path.insert(path.end(), module_path.begin(), module_path.end());
  path.push_back(std::to_string(index));
  return core::Mangle(core::StringOfPath(path));
}

bool IsMoveExprLite(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

bool IsPlaceExprLite(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return IsPlaceExprLite(node.value);
        }
        return false;
      },
      expr->node);
}

}  // namespace

// ============================================================================
// Section 6.7 Static Initializer Helpers
// ============================================================================

bool StaticHasResponsibility(const ast::StaticDecl& item) {
  const auto& init = item.binding.init;
  if (!init) {
    return true;
  }
  if (!IsPlaceExprLite(init)) {
    return true;
  }
  return IsMoveExprLite(init);
}

std::vector<const ast::Expr*> StaticInitExprs(const ast::ASTModule& module) {
  std::vector<const ast::Expr*> exprs;
  for (const auto& item : module.items) {
    if (const auto* static_decl = std::get_if<ast::StaticDecl>(&item)) {
      if (static_decl->binding.init) {
        exprs.push_back(static_decl->binding.init.get());
      }
    }
  }
  return exprs;
}

std::vector<const ast::Expr*> InitList(const ast::ASTModule& module) {
  return StaticInitExprs(module);
}

// ============================================================================
// Helper functions
// ============================================================================

// SeqIRList([]) = empty
// SeqIRList([IR] ++ IRs) = SeqIR(IR, SeqIRList(IRs))
static IRPtr SeqIRList(const std::vector<IRPtr>& irs) {
  SPEC_DEF("SeqIRList", "");
  return SeqIR(std::vector<IRPtr>(irs.begin(), irs.end()));
}

// Rev([]) = []
// Rev([x] ++ xs) = Rev(xs) ++ [x]
template <typename T>
static std::vector<T> Rev(const std::vector<T>& vec) {
  SPEC_DEF("Rev", "");
  std::vector<T> result(vec.rbegin(), vec.rend());
  return result;
}

void RegisterInitializedStaticCleanup(const ast::ModulePath& module_path,
                                      const ast::StaticDecl& item,
                                      LowerCtx& ctx) {
  if (!StaticHasResponsibility(item)) {
    return;
  }

  const auto bind_types = StaticBindTypes(item.binding, module_path, ctx);
  for (const auto& [name, type] : bind_types) {
    if (!TypeNeedsDrop(type, ctx)) {
      continue;
    }

    LowerCtx::StaticInitCleanup cleanup;
    cleanup.module_path = module_path;
    cleanup.name = name;
    cleanup.type = type;
    ctx.active_static_init_cleanup.push_back(std::move(cleanup));
  }
}

CleanupPlan ActiveStaticInitCleanupPlan(const LowerCtx& ctx) {
  CleanupPlan plan;
  plan.reserve(ctx.active_static_init_cleanup.size());
  for (auto it = ctx.active_static_init_cleanup.rbegin();
       it != ctx.active_static_init_cleanup.rend();
       ++it) {
    CleanupAction action;
    action.kind = CleanupAction::Kind::DropStatic;
    action.name = it->name;
    action.type = it->type;
    action.static_module_path = it->module_path;
    plan.push_back(std::move(action));
  }
  return plan;
}

static std::vector<ast::ModulePath> TopoOrderFromEdges(
    const std::vector<ast::ModulePath>& modules,
    const std::vector<std::pair<std::size_t, std::size_t>>& edges) {
  const std::size_t n = modules.size();
  if (n == 0) {
    return {};
  }
  std::vector<std::vector<std::size_t>> adj(n);
  std::vector<std::size_t> indeg(n, 0);
  for (const auto& edge : edges) {
    if (edge.first >= n || edge.second >= n) {
      continue;
    }
    adj[edge.first].push_back(edge.second);
    indeg[edge.second] += 1;
  }
  std::set<std::size_t> ready;
  for (std::size_t i = 0; i < n; ++i) {
    if (indeg[i] == 0) {
      ready.insert(i);
    }
  }
  std::vector<ast::ModulePath> order;
  order.reserve(n);
  while (!ready.empty()) {
    const std::size_t u = *ready.begin();
    ready.erase(ready.begin());
    order.push_back(modules[u]);
    for (const auto v : adj[u]) {
      if (indeg[v] == 0) {
        continue;
      }
      indeg[v] -= 1;
      if (indeg[v] == 0) {
        ready.insert(v);
      }
    }
  }
  if (order.size() != n) {
    return {};
  }
  return order;
}

// ============================================================================
// Section 6.7 Init/Deinit Symbol Generation
// ============================================================================

std::string InitSym(const ast::ModulePath& module_path) {
  SPEC_DEF("InitSym", "");

  // InitSym(m) = PathSig(["ultraviolet", "runtime", "init"] ++ PathOfModule(m))
  std::vector<std::string> path = {
      std::string(project::ActiveLanguageProfile().runtime_root),
      "runtime",
      "init"};
  path.insert(path.end(), module_path.begin(), module_path.end());
  return core::Mangle(core::StringOfPath(path));
}

std::string DeinitSym(const ast::ModulePath& module_path) {
  SPEC_DEF("DeinitSym", "");

  // DeinitSym(m) = PathSig(["ultraviolet", "runtime", "deinit"] ++ PathOfModule(m))
  std::vector<std::string> path = {
      std::string(project::ActiveLanguageProfile().runtime_root),
      "runtime",
      "deinit"};
  path.insert(path.end(), module_path.begin(), module_path.end());
  return core::Mangle(core::StringOfPath(path));
}

std::string InitFn(const ast::ModulePath& module_path) {
  SPEC_RULE("InitFn");
  return InitSym(module_path);
}

std::string DeinitFn(const ast::ModulePath& module_path) {
  SPEC_RULE("DeinitFn");
  return DeinitSym(module_path);
}

// ============================================================================
// Section 6.7 Static Init Lowering
// ============================================================================

IRPtr LowerStaticInitItem(const ast::ModulePath& module_path,
                          const ast::StaticDecl& item,
                          LowerCtx& ctx) {
  SPEC_RULE("Lower-StaticInit-Item");

  const auto& binding = item.binding;
  if (!binding.init) {
    return EmptyIR();
  }

  std::vector<IRPtr> ir_parts;

  auto init_result = LowerExpr(*binding.init, ctx);
  ir_parts.push_back(init_result.ir);

  std::vector<std::pair<std::string, IRValue>> binds;
  if (binding.pat) {
    binds = PatternBindingValuesInOrder(*binding.pat, init_result.value, ctx);
  }

  ir_parts.push_back(StaticStoreIR(item, module_path, binds));
  RegisterInitializedStaticCleanup(module_path, item, ctx);
  ir_parts.push_back(InitPanicHandle(ModulePathString(module_path), ctx));

  return SeqIR(std::move(ir_parts));
}

IRPtr LowerStaticInitItems(const ast::ModulePath& module_path,
                           const std::vector<const ast::StaticDecl*>& items,
                           LowerCtx& ctx) {
  // (Lower-StaticInitItems-Empty)
  if (items.empty()) {
    SPEC_RULE("Lower-StaticInitItems-Empty");
    return EmptyIR();
  }

  // (Lower-StaticInitItems-Cons)
  SPEC_RULE("Lower-StaticInitItems-Cons");

  std::vector<IRPtr> ir_parts;
  ir_parts.reserve(items.size());

  for (const auto* item : items) {
    ir_parts.push_back(LowerStaticInitItem(module_path, *item, ctx));
  }

  return SeqIRList(ir_parts);
}

IRPtr LowerStaticInit(const ast::ModulePath& module_path,
                      const ast::ASTModule& module,
                      LowerCtx& ctx) {
  SPEC_RULE("Lower-StaticInit");

  // (Lower-StaticInit)
  // StaticItems(Project(Gamma), m) = items
  // Lower-StaticInitItems(m, items) => IR
  // Lower-StaticInit(m) => IR

  auto items = StaticItems(module);
  return LowerStaticInitItems(module_path, items, ctx);
}

// ============================================================================
// Section 6.7 Static Deinit Lowering
// ============================================================================

IRPtr LowerStaticDeinitNames(const ast::ModulePath& module_path,
                             const ast::StaticDecl& item,
                             const std::vector<std::string>& names,
                             LowerCtx& ctx) {
  if (names.empty()) {
    SPEC_RULE("Lower-StaticDeinitNames-Empty");
    return EmptyIR();
  }

  std::vector<IRPtr> ir_parts;
  ir_parts.reserve(names.size());

  for (const auto& name : names) {
    const auto bind_info =
        ctx.sigma ? StaticBindInfo(*ctx.sigma, module_path, name) : std::nullopt;
    if (!bind_info.has_value() || !bind_info->has_responsibility) {
      SPEC_RULE("Lower-StaticDeinitNames-Cons-NoResp");
      continue;
    }
    SPEC_RULE("Lower-StaticDeinitNames-Cons-Resp");

    analysis::TypeRef type = bind_info->type;
    IRValue loaded_value;
    loaded_value.kind = IRValue::Kind::Symbol;
    if (ctx.sigma) {
      if (auto addr = StaticAddr(*ctx.sigma, module_path, name)) {
        loaded_value = *addr;
      } else {
        loaded_value.name = StaticSymPath(module_path, name);
      }
    } else {
      loaded_value.name = StaticSymPath(module_path, name);
    }
    ctx.RegisterValueType(loaded_value, type);

    IRPtr drop_ir = EmitDrop(type, loaded_value, ctx);
    ir_parts.push_back(drop_ir);
  }

  if (ir_parts.empty()) {
    return EmptyIR();
  }
  return SeqIRList(ir_parts);
}

IRPtr LowerStaticDeinitItem(const ast::ModulePath& module_path,
                            const ast::StaticDecl& item,
                            LowerCtx& ctx) {
  SPEC_RULE("Lower-StaticDeinit-Item");

  // (Lower-StaticDeinit-Item)
  // item = StaticDecl(vis, mut, binding, span, doc)
  // binding = (pat, _, _, _, _)
  // xs = Rev(StaticBindList(binding))
  // Lower-StaticDeinitNames(PathOfModule(m), item, xs) => IR
  // Lower-StaticDeinitItem(m, item) => IR

  auto names = StaticBindList(item.binding);
  auto reversed_names = Rev(names);

  return LowerStaticDeinitNames(module_path, item, reversed_names, ctx);
}

IRPtr LowerStaticDeinitItems(const ast::ModulePath& module_path,
                             const std::vector<const ast::StaticDecl*>& items,
                             LowerCtx& ctx) {
  // (Lower-StaticDeinitItems-Empty)
  if (items.empty()) {
    SPEC_RULE("Lower-StaticDeinitItems-Empty");
    return EmptyIR();
  }

  // (Lower-StaticDeinitItems-Cons)
  SPEC_RULE("Lower-StaticDeinitItems-Cons");

  std::vector<IRPtr> ir_parts;
  ir_parts.reserve(items.size());

  for (const auto* item : items) {
    ir_parts.push_back(LowerStaticDeinitItem(module_path, *item, ctx));
  }

  return SeqIRList(ir_parts);
}

IRPtr LowerStaticDeinit(const ast::ModulePath& module_path,
                        const ast::ASTModule& module,
                        LowerCtx& ctx) {
  SPEC_RULE("Lower-StaticDeinit");

  // (Lower-StaticDeinit)
  // StaticItems(Project(Gamma), m) = items
  // Lower-StaticDeinitItems(m, Rev(items)) => IR
  // Lower-StaticDeinit(m) => IR

  auto items = StaticItems(module);
  auto reversed_items = Rev(items);

  return LowerStaticDeinitItems(module_path, reversed_items, ctx);
}

IRPtr InitItemCallIR(const std::string& sym) {
  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = sym;

  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  call.args.push_back(panic_out);
  return MakeIR(std::move(call));
}

// ============================================================================
// Section 6.7 Init/Deinit Call IR
// ============================================================================

IRPtr InitCallIR(const ast::ModulePath& module_path, LowerCtx& ctx) {
  SPEC_RULE("InitCallIR");

  // (InitCallIR)
  // InitFn(m) => sym
  // InitCallIR(m) => SeqIR(CallIR(sym, [PanicOutName]), PanicCheck)

  std::string sym = InitFn(module_path);

  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = sym;

  // Add PanicOutName as argument
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  call.args.push_back(panic_out);

  std::vector<IRPtr> parts;
  parts.push_back(MakeIR(std::move(call)));
  parts.push_back(PanicCheck(ctx));

  return SeqIR(std::move(parts));
}

IRPtr DeinitCallIR(const ast::ModulePath& module_path, LowerCtx& ctx) {
  SPEC_RULE("DeinitCallIR");

  // (DeinitCallIR)
  // DeinitFn(m) => sym
  // DeinitCallIR(m) => SeqIR(CallIR(sym, [PanicOutName]), PanicCheck)

  std::string sym = DeinitFn(module_path);

  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = sym;

  // Add PanicOutName as argument
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  call.args.push_back(panic_out);

  std::vector<IRPtr> parts;
  parts.push_back(MakeIR(std::move(call)));
  parts.push_back(PanicCheck(ctx));

  return SeqIR(std::move(parts));
}

// ============================================================================
// Section 6.7 Init/Deinit Plan Generation
// ============================================================================

IRPtr EmitInitPlan(const std::vector<ast::ModulePath>& init_order,
                   LowerCtx& ctx) {
  SPEC_RULE("EmitInitPlan");

  // (EmitInitPlan)
  // InitOrder = [m_1,...,m_k]
  // For all i, InitCallIR(m_i) => IR_i
  // IR_init = SeqIRList([IR_1,...,IR_k])
  // EmitInitPlan(P) => IR_init

  std::vector<ast::ModulePath> order = init_order;
  if (order.empty()) {
    if (!ctx.init_modules.empty()) {
      const auto fallback =
          TopoOrderFromEdges(ctx.init_modules, ctx.init_eager_edges);
      if (!fallback.empty()) {
        order = fallback;
      } else {
        order = ctx.init_modules;
      }
    } else if (ctx.sigma) {
      order.reserve(ctx.sigma->mods.size());
      for (const auto& mod : ctx.sigma->mods) {
        order.push_back(mod.path);
      }
    }
  } else if (!ctx.init_modules.empty() &&
             order.size() != ctx.init_modules.size()) {
    const auto fallback =
        TopoOrderFromEdges(ctx.init_modules, ctx.init_eager_edges);
    if (!fallback.empty()) {
      order = fallback;
    }
  }

  std::vector<IRPtr> ir_parts;
  ir_parts.reserve(order.size());

  for (const auto& module_path : order) {
    ir_parts.push_back(InitCallIR(module_path, ctx));
  }

  return SeqIRList(ir_parts);
}

IRPtr EmitDeinitPlan(const std::vector<ast::ModulePath>& init_order,
                     LowerCtx& ctx) {
  SPEC_RULE("EmitDeinitPlan");

  // (EmitDeinitPlan)
  // InitOrder = [m_1,...,m_k]
  // For all i, DeinitCallIR(m_i) => IR_i
  // IR_deinit = SeqIRList(Rev([IR_1,...,IR_k]))
  // EmitDeinitPlan(P) => IR_deinit

  std::vector<ast::ModulePath> order = init_order;
  if (order.empty()) {
    if (!ctx.init_modules.empty()) {
      const auto fallback =
          TopoOrderFromEdges(ctx.init_modules, ctx.init_eager_edges);
      if (!fallback.empty()) {
        order = fallback;
      } else {
        order = ctx.init_modules;
      }
    } else if (ctx.sigma) {
      order.reserve(ctx.sigma->mods.size());
      for (const auto& mod : ctx.sigma->mods) {
        order.push_back(mod.path);
      }
    }
  } else if (!ctx.init_modules.empty() &&
             order.size() != ctx.init_modules.size()) {
    const auto fallback =
        TopoOrderFromEdges(ctx.init_modules, ctx.init_eager_edges);
    if (!fallback.empty()) {
      order = fallback;
    }
  }

  auto reversed_order = Rev(order);

  std::vector<IRPtr> ir_parts;
  ir_parts.reserve(reversed_order.size());

  for (const auto& module_path : reversed_order) {
    ir_parts.push_back(DeinitCallIR(module_path, ctx));
  }
  return SeqIRList(ir_parts);
}

// ============================================================================
// Section 6.7 Module Init/Deinit Function IR
// ============================================================================

ProcIR EmitModuleInitFn(const ast::ModulePath& module_path,
                        const ast::ASTModule& module,
                        LowerCtx& ctx) {
  SPEC_DEF("EmitModuleInitFn", "");

  ProcIR proc;
  proc.symbol = InitFn(module_path);

  // Init function takes a panic out parameter
  proc.params.push_back(PanicOutParam());

  // Return type is unit
  proc.ret = analysis::MakeTypePrim("()");

  const auto saved_init_cleanup = std::move(ctx.active_static_init_cleanup);
  const auto saved_active_init_module = std::move(ctx.active_static_init_module);
  ctx.active_static_init_cleanup.clear();
  ctx.active_static_init_module = ModulePathString(module_path);
  proc.body = LowerStaticInit(module_path, module, ctx);
  ctx.active_static_init_cleanup = saved_init_cleanup;
  ctx.active_static_init_module = saved_active_init_module;

  return proc;
}

ProcIR EmitModuleDeinitFn(const ast::ModulePath& module_path,
                          const ast::ASTModule& module,
                          LowerCtx& ctx) {
  SPEC_DEF("EmitModuleDeinitFn", "");

  ProcIR proc;
  proc.symbol = DeinitFn(module_path);

  // Deinit function takes a panic out parameter
  proc.params.push_back(PanicOutParam());

  // Return type is unit
  proc.ret = analysis::MakeTypePrim("()");

  // Body is the static deinit lowering
  proc.body = LowerStaticDeinit(module_path, module, ctx);

  return proc;
}

// ============================================================================
// Section 6.7 Module Dependency Analysis
// ============================================================================

std::vector<std::size_t> ValueDepsEager(
    const ast::ModulePath& module_path,
    const ast::ASTModule& module,
    const std::vector<ast::ModulePath>& all_modules,
    const analysis::Sigma& sigma) {
  (void)sigma;

  std::unordered_map<std::string, std::size_t> module_index;
  module_index.reserve(all_modules.size());
  for (std::size_t i = 0; i < all_modules.size(); ++i) {
    module_index.emplace(ModulePathString(all_modules[i]), i);
  }

  const std::string self_module = ModulePathString(module_path);
  std::unordered_set<std::size_t> deps_set;
  deps_set.reserve(all_modules.size());

  auto add_module_dep = [&](std::size_t idx) {
    if (idx >= all_modules.size()) {
      return;
    }
    if (ModulePathString(all_modules[idx]) == self_module) {
      return;
    }
    deps_set.insert(idx);
  };

  auto add_path_dep = [&](const ast::ModulePath& path, bool drop_last) {
    if (path.empty()) {
      return;
    }
    std::size_t end = path.size();
    if (drop_last) {
      if (end <= 1) {
        return;
      }
      --end;
    }
    if (end == 0) {
      return;
    }

    std::string candidate;
    std::optional<std::size_t> best_idx;
    for (std::size_t i = 0; i < end; ++i) {
      if (i > 0) {
        candidate.append("::");
      }
      candidate.append(path[i]);
      const auto it = module_index.find(candidate);
      if (it != module_index.end()) {
        best_idx = it->second;
      }
    }
    if (best_idx.has_value()) {
      add_module_dep(*best_idx);
    }
  };

  std::function<void(const ast::ExprPtr&)> scan_expr;
  std::function<void(const ast::Stmt&)> scan_stmt;
  std::function<void(const ast::BlockPtr&)> scan_block;

  auto scan_args = [&](const std::vector<ast::Arg>& args) {
    for (const auto& arg : args) {
      scan_expr(arg.value);
    }
  };

  auto scan_fields = [&](const std::vector<ast::FieldInit>& fields) {
    for (const auto& field : fields) {
      scan_expr(field.value);
    }
  };

  scan_block = [&](const ast::BlockPtr& block) {
    if (!block) {
      return;
    }
    for (const auto& stmt : block->stmts) {
      scan_stmt(stmt);
    }
    scan_expr(block->tail_opt);
  };

  scan_stmt = [&](const ast::Stmt& stmt) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            scan_expr(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            (void)node;
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            scan_expr(node.place);
            scan_expr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            scan_expr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>) {
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            scan_expr(node.opts_opt);
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            scan_expr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& key_path : node.paths) {
              for (const auto& seg : key_path.segs) {
                if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
                  scan_expr(idx->expr);
                }
              }
            }
            scan_block(node.body);
          }
        },
        stmt);
  };

  scan_expr = [&](const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
            add_path_dep(node.path, false);
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            add_path_dep(node.path, false);
            if (const auto* paren = std::get_if<ast::ParenArgs>(&node.args)) {
              scan_args(paren->args);
            } else if (const auto* brace = std::get_if<ast::BraceArgs>(&node.args)) {
              scan_fields(brace->fields);
            }
          } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
            add_path_dep(node.path, false);
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            add_path_dep(node.path, true);
            if (node.payload_opt.has_value()) {
              if (const auto* tuple_payload =
                      std::get_if<ast::EnumPayloadParen>(&*node.payload_opt)) {
                for (const auto& elem : tuple_payload->elements) {
                  scan_expr(elem);
                }
              } else if (const auto* record_payload =
                             std::get_if<ast::EnumPayloadBrace>(&*node.payload_opt)) {
                scan_fields(record_payload->fields);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            scan_expr(node.lhs);
            scan_expr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            scan_expr(node.lhs);
            scan_expr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::AddressOfExpr> ||
                               std::is_same_v<T, ast::MoveExpr> ||
                               std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr> ||
                               std::is_same_v<T, ast::WaitExpr>) {
            if constexpr (std::is_same_v<T, ast::CastExpr> ||
                          std::is_same_v<T, ast::UnaryExpr> ||
                          std::is_same_v<T, ast::DerefExpr> ||
                          std::is_same_v<T, ast::PropagateExpr> ||
                          std::is_same_v<T, ast::YieldExpr> ||
                          std::is_same_v<T, ast::YieldFromExpr> ||
                          std::is_same_v<T, ast::SyncExpr>) {
              scan_expr(node.value);
            } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                                 std::is_same_v<T, ast::MoveExpr>) {
              scan_expr(node.place);
            } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
              scan_expr(node.value);
            } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
              scan_expr(node.handle);
            }
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            scan_expr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              scan_expr(elem);
            }
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
              scan_expr(elem);
            });
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            scan_expr(node.value);
            scan_expr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            std::visit(
                [&](const auto& target) {
                  using TargetT = std::decay_t<decltype(target)>;
                  if constexpr (std::is_same_v<TargetT, ast::TypePath>) {
                    add_path_dep(target, true);
                  } else if constexpr (std::is_same_v<TargetT, ast::ModalStateRef>) {
                    add_path_dep(target.path, true);
                  }
                },
                node.target);
            scan_fields(node.fields);
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            scan_expr(node.cond);
            scan_expr(node.then_expr);
            scan_expr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            scan_expr(node.scrutinee);
            for (const auto& case_clause : node.cases) {
              scan_expr(case_clause.body);
            }
            scan_expr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            scan_expr(node.scrutinee);
            scan_expr(node.then_expr);
            scan_expr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            scan_expr(node.cond);
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            scan_expr(node.iter);
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            scan_block(node.block);
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            scan_expr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
            scan_expr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            scan_expr(node.body);
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            scan_expr(node.lhs);
            scan_expr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            scan_expr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            scan_expr(node.base);
            scan_expr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            scan_expr(node.callee);
            scan_args(node.args);
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            scan_expr(node.receiver);
            scan_args(node.args);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              scan_expr(arm.expr);
              scan_expr(arm.handler.value);
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& elem : node.exprs) {
              scan_expr(elem);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            scan_expr(node.domain);
            for (const auto& opt : node.opts) {
              scan_expr(opt.value);
            }
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              scan_expr(opt.value);
            }
            scan_block(node.body);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            scan_expr(node.range);
            if (node.key_clause.has_value()) {
              for (const auto& seg : node.key_clause->key_path.segs) {
                if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
                  scan_expr(idx->expr);
                }
              }
            }
            for (const auto& opt : node.opts) {
              scan_expr(opt.chunk_expr);
              scan_expr(opt.workgroup_expr);
            }
            scan_block(node.body);
          }
        },
        expr->node);
  };

  for (const auto& item : module.items) {
    if (const auto* import_decl = std::get_if<ast::ImportDecl>(&item)) {
      add_path_dep(import_decl->path, false);
      continue;
    }
    if (const auto* static_decl = std::get_if<ast::StaticDecl>(&item)) {
      scan_expr(static_decl->binding.init);
    }
  }

  std::vector<std::size_t> deps(deps_set.begin(), deps_set.end());
  std::sort(deps.begin(), deps.end());
  return deps;
}

std::vector<ast::ModulePath> ComputeInitOrder(
    const std::vector<ast::ModulePath>& modules,
    const std::vector<ModuleDepEdge>& edges) {
  return TopoOrderFromEdges(modules, edges);
}

std::vector<ast::ModulePath> ComputeInitOrderFromSigma(
    const analysis::Sigma& sigma) {
  std::vector<ast::ModulePath> modules;
  modules.reserve(sigma.mods.size());
  for (const auto& mod : sigma.mods) {
    modules.push_back(mod.path);
  }
  std::vector<ModuleDepEdge> edges;
  for (std::size_t i = 0; i < sigma.mods.size(); ++i) {
    const auto deps = ValueDepsEager(
        sigma.mods[i].path, sigma.mods[i], modules, sigma);
    for (const auto dep : deps) {
      edges.emplace_back(dep, i);
    }
  }

  const auto order = ComputeInitOrder(modules, edges);
  if (!order.empty() || modules.empty()) {
    return order;
  }
  return modules;
}

// ============================================================================
// Section A7.1 Initialization State Tracking
// ============================================================================

bool ModuleInitTracker::AdvanceToNext() {
  if (current_index + 1 < order.size()) {
    current_index++;
    return true;
  }
  return false;
}

void ModuleInitTracker::MarkReady() {
  if (current_index < states.size()) {
    states[current_index] = ModuleInitState::Ready;
  }
}

void ModuleInitTracker::MarkPoisoned(const std::string& reason) {
  if (current_index < states.size()) {
    states[current_index] = ModuleInitState::Poisoned;
  }
  panicked = true;
  if (!panic_module.has_value() && current_index < order.size()) {
    panic_module = ModulePathString(order[current_index]);
  }
}

bool ModuleInitTracker::AllReady() const {
  for (const auto& state : states) {
    if (state != ModuleInitState::Ready) {
      return false;
    }
  }
  return true;
}

// ============================================================================
// Section 6.7 Poison Checking
// ============================================================================

IRPtr EmitCheckPoisonIR(const std::string& module_name) {
  IRCheckPoison check;
  check.module = module_name;
  return MakeIR(std::move(check));
}

// ============================================================================
// Section 6.7 Initialization Order Validation
// ============================================================================

bool ValidateInitOrder(const std::vector<ast::ModulePath>& order,
                       const std::vector<ModuleDepEdge>& edges,
                       const std::vector<ast::ModulePath>& all_modules) {
  // Build position map
  std::unordered_map<std::string, std::size_t> pos_map;
  for (std::size_t i = 0; i < order.size(); ++i) {
    pos_map[ModulePathString(order[i])] = i;
  }

  // Check all edges: source should come before target
  for (const auto& [from_idx, to_idx] : edges) {
    if (from_idx >= all_modules.size() || to_idx >= all_modules.size()) {
      continue;
    }
    std::string from_str = ModulePathString(all_modules[from_idx]);
    std::string to_str = ModulePathString(all_modules[to_idx]);

    auto from_it = pos_map.find(from_str);
    auto to_it = pos_map.find(to_str);
    if (from_it == pos_map.end() || to_it == pos_map.end()) {
      return false;
    }
    if (from_it->second >= to_it->second) {
      return false;  // from should come before to
    }
  }
  return true;
}

std::vector<ast::ModulePath> DetectInitCycle(
    const std::vector<ast::ModulePath>& modules,
    const std::vector<ModuleDepEdge>& edges) {
  const auto order = TopoOrderFromEdges(modules, edges);
  if (order.size() == modules.size()) {
    return {};
  }

  const std::size_t n = modules.size();
  std::vector<std::vector<std::size_t>> adj(n);
  for (const auto& [from_idx, to_idx] : edges) {
    if (from_idx >= n || to_idx >= n) {
      continue;
    }
    adj[from_idx].push_back(to_idx);
  }

  std::vector<int> color(n, 0);
  std::vector<std::size_t> stack;
  std::vector<std::size_t> cycle_indices;

  std::function<bool(std::size_t)> dfs = [&](std::size_t u) -> bool {
    color[u] = 1;
    stack.push_back(u);

    for (const auto v : adj[u]) {
      if (color[v] == 0) {
        if (dfs(v)) {
          return true;
        }
        continue;
      }
      if (color[v] != 1) {
        continue;
      }

      const auto it = std::find(stack.begin(), stack.end(), v);
      if (it == stack.end()) {
        continue;
      }
      cycle_indices.assign(it, stack.end());
      cycle_indices.push_back(v);
      return true;
    }

    stack.pop_back();
    color[u] = 2;
    return false;
  };

  for (std::size_t i = 0; i < n; ++i) {
    if (color[i] != 0) {
      continue;
    }
    if (dfs(i)) {
      break;
    }
  }

  std::vector<ast::ModulePath> cycle;
  cycle.reserve(cycle_indices.size());
  for (const auto idx : cycle_indices) {
    if (idx < modules.size()) {
      cycle.push_back(modules[idx]);
    }
  }
  return cycle;
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorInitRules() {
  // Section 6.7 Init/Deinit
  SPEC_RULE("InitFn");
  SPEC_RULE("DeinitFn");
  SPEC_RULE("Lower-StaticInit-Item");
  SPEC_RULE("Lower-StaticInitItems-Empty");
  SPEC_RULE("Lower-StaticInitItems-Cons");
  SPEC_RULE("Lower-StaticInit");
  SPEC_RULE("Lower-StaticDeinitNames-Empty");
  SPEC_RULE("Lower-StaticDeinitNames-Cons-Resp");
  SPEC_RULE("Lower-StaticDeinitNames-Cons-NoResp");
  SPEC_RULE("Lower-StaticDeinit-Item");
  SPEC_RULE("Lower-StaticDeinitItems-Empty");
  SPEC_RULE("Lower-StaticDeinitItems-Cons");
  SPEC_RULE("Lower-StaticDeinit");
  SPEC_RULE("InitCallIR");
  SPEC_RULE("DeinitCallIR");
  SPEC_RULE("EmitInitPlan");
  SPEC_RULE("EmitInitPlan-Err");
  SPEC_RULE("EmitDeinitPlan");
  SPEC_RULE("EmitDeinitPlan-Err");

  // A7.1 Initialization Order and Poisoning
  SPEC_RULE("Init-Start");
  SPEC_RULE("Init-Step");
  SPEC_RULE("Init-Next-Module");
  SPEC_RULE("Init-Panic");
  SPEC_RULE("Init-Done");
  SPEC_RULE("Init-Ok");
  SPEC_RULE("Init-Fail");
  SPEC_RULE("Deinit-Ok");
  SPEC_RULE("Deinit-Panic");

  // Definitions
  SPEC_DEF("InitSym", "");
  SPEC_DEF("DeinitSym", "");
  SPEC_DEF("SeqIRList", "");
  SPEC_DEF("Rev", "");
  SPEC_DEF("EmitModuleInitFn", "");
  SPEC_DEF("EmitModuleDeinitFn", "");
}

}  // namespace ultraviolet::codegen
