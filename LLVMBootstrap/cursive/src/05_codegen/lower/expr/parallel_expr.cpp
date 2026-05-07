// =============================================================================
// MIGRATION MAPPING: expr/parallel_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 10 (Structured Parallelism)
//   - Parallel blocks with domain specification
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - ParallelExpr visitor produces IRParallel
//   - Capture analysis for parallel closures
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir_model.h (IRParallel)
//   - cursive/src/04_analysis/caps/cap_concurrency.h
//
// =============================================================================

#include "05_codegen/lower/expr/parallel_expr.h"

#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/typing/types.h"
#include "00_core/process_config.h"
#include "00_core/spec_trace.h"
#include <cstdio>
#include <cstdlib>

namespace cursive::codegen {

namespace {

// Check if a dispatch expression has a reduce option.
bool DispatchHasReduce(const ast::DispatchExpr& expr) {
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

// Check if an expression is collectable for parallel block result collection.
// Spawn expressions and dispatch expressions with reduce options are collectable.
// Sets needs_wait to true for spawn expressions (which require wait).
bool IsCollectableParallelExpr(const ast::Expr& expr, bool& needs_wait) {
  if (std::holds_alternative<ast::SpawnExpr>(expr.node)) {
    needs_wait = true;
    return true;
  }
  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr.node)) {
    if (DispatchHasReduce(*dispatch)) {
      needs_wait = false;
      return true;
    }
  }
  return false;
}

}  // namespace

// =============================================================================
// LowerParallelExpr - Lower a parallel expression
// =============================================================================
//
// A parallel expression consists of:
//   - A domain expression ($ExecutionDomain)
//   - Optional options (cancel token, name)
//   - A body block containing spawn/dispatch expressions
//
// Lowering steps:
//   1. Lower the domain expression
//   2. Set up parallel_collect for collectable spawn/dispatch expressions
//   3. Lower the body block
//   4. Create IRParallel with domain, body, result
//   5. Handle options: cancel token and name
//   6. If explicit result (tail not collectable), use body result
//   7. Otherwise collect spawned values and wait for them, create tuple result
//
LowerResult LowerParallelExpr(const ast::ParallelExpr& node, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Parallel");

  // Lower domain expression
  auto domain_result = LowerExpr(*node.domain, ctx);
  IRValue parallel_ctx = ctx.FreshTempValue("parallel_join");

  // The parallel boundary must await started work even when the body exits
  // via return/break/continue, so model the join as scope cleanup.
  ctx.PushScope(false, false);
  ctx.RegisterParallelJoin(parallel_ctx);

  // Lower body
  std::vector<ParallelCollectItem> collected;
  auto* prev_collect = ctx.parallel_collect;
  int prev_depth = ctx.parallel_collect_depth;
  bool explicit_result = false;

  if (node.body) {
    if (node.body->tail_opt) {
      bool needs_wait = false;
      if (!IsCollectableParallelExpr(*node.body->tail_opt, needs_wait)) {
        explicit_result = true;
      }
    }
  }

  if (!explicit_result) {
    ctx.parallel_collect = &collected;
    ctx.parallel_collect_depth = 0;
  }

  LowerResult body_result;
  if (node.body) {
    body_result = LowerBlock(*node.body, ctx);
  } else {
    body_result = LowerResult{EmptyIR(), ctx.FreshTempValue("parallel_unit")};
  }

  ctx.parallel_collect = prev_collect;
  ctx.parallel_collect_depth = prev_depth;

  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  IRPtr join_ir = EmitCleanup(cleanup_plan, ctx);
  ctx.PopScope();

  IRPtr parallel_body = body_result.ir;
  IRValue result_value;
  if (explicit_result) {
    result_value = body_result.value;
  } else if (collected.empty()) {
    result_value = ctx.FreshTempValue("unit");
  } else {
    std::vector<IRPtr> body_parts;
    if (body_result.ir &&
        !std::holds_alternative<IROpaque>(body_result.ir->node)) {
      body_parts.push_back(body_result.ir);
    }
    std::vector<IRValue> elems;
    elems.reserve(collected.size());
    for (const auto& item : collected) {
      if (item.needs_wait) {
        IRWait wait;
        wait.handle = item.value;
        wait.result = ctx.FreshTempValue("parallel_wait");
        analysis::TypeRef wait_type = item.value_type;
        if (!wait_type) {
          analysis::TypeRef handle_type = ctx.LookupValueType(item.value);
          analysis::TypeRef stripped = analysis::StripPerm(handle_type);
          if (!stripped) {
            stripped = handle_type;
          }
          if (const auto inner = analysis::ExtractSpawnedInner(stripped)) {
            wait_type = *inner;
          }
        }
        if (wait_type) {
          ctx.RegisterValueType(wait.result, wait_type);
        }
        IRValue wait_result = wait.result;
        body_parts.push_back(MakeIR(std::move(wait)));
        elems.push_back(wait_result);
      } else {
        elems.push_back(item.value);
      }
    }
    parallel_body = SeqIR(std::move(body_parts));
    if (elems.size() == 1) {
      result_value = elems[0];
    } else {
      IRValue tuple_value = ctx.FreshTempValue("parallel_tuple");
      DerivedValueInfo info;
      info.kind = DerivedValueInfo::Kind::TupleLit;
      info.elements = std::move(elems);
      ctx.RegisterDerivedValue(tuple_value, info);
      std::vector<analysis::TypeRef> elem_types;
      bool all_typed = true;
      for (const auto& elem : info.elements) {
        analysis::TypeRef elem_type = ctx.LookupValueType(elem);
        if (!elem_type) {
          all_typed = false;
          break;
        }
        elem_types.push_back(elem_type);
      }
      if (all_typed) {
        ctx.RegisterValueType(tuple_value, analysis::MakeTypeTuple(std::move(elem_types)));
      }
      result_value = tuple_value;
    }
  }

  if (core::IsDebugEnabled("parallel")) {
    std::fprintf(stderr,
                 "[parallel-collect] explicit=%d collected=%zu result=%s\n",
                 explicit_result ? 1 : 0,
                 collected.size(),
                 result_value.name.c_str());
    for (std::size_t i = 0; i < collected.size(); ++i) {
      std::fprintf(stderr,
                   "[parallel-collect] item[%zu] name=%s wait=%d type=%s\n",
                   i,
                   collected[i].value.name.c_str(),
                   collected[i].needs_wait ? 1 : 0,
                   collected[i].value_type
                       ? analysis::TypeToString(collected[i].value_type).c_str()
                       : "<null>");
    }
  }

  // Create IRParallel
  IRParallel parallel;
  parallel.domain = domain_result.value;
  parallel.body = parallel_body;
  parallel.result = parallel_ctx;

  // Handle options (cancel, name)
  for (const auto& opt : node.opts) {
    if (opt.kind == ast::ParallelOptionKind::Cancel && opt.value) {
      auto cancel_result = LowerExpr(*opt.value, ctx);
      parallel.cancel_token = cancel_result.value;
    } else if (opt.kind == ast::ParallelOptionKind::Name && opt.value) {
      if (const auto* lit = std::get_if<ast::LiteralExpr>(&opt.value->node)) {
        parallel.name = lit->literal.lexeme;
      }
    }
  }

  std::vector<IRPtr> parts;
  parts.push_back(domain_result.ir);
  parts.push_back(MakeIR(std::move(parallel)));
  parts.push_back(join_ir);

  return LowerResult{SeqIR(std::move(parts)), result_value};
}

}  // namespace cursive::codegen
