// =============================================================================
// MIGRATION MAPPING: lower_proc.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6 (Code Generation)
//   - Lines 16586-16756: Statement and Block Lowering (for block handling)
//   - Lines 17028-17160: Cleanup, Drop, and Unwinding
//   - Procedure lowering handles function body generation and async procedures
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_proc.cpp
//   - Lines 1-18: Includes and namespace
//   - Lines 22-66: Helper functions (IsUnitType, IsAsyncProc, BlockEndsWithReturn, etc.)
//   - Lines 67-148: AsyncIRInfo struct and CollectAsyncIR - async state machine analysis
//   - Lines 150-186: Contract check emission (EmitPreconditionCheck, EmitPostconditionCheck)
//   - Lines 190-430: LowerProc - main procedure lowering function
//   - Lines 432-434: AnchorProcRules
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/lower/lower_expr.h (LowerExpr)
//   - ultraviolet/src/05_codegen/lower/lower_stmt.h (LowerBlock)
//   - ultraviolet/src/05_codegen/abi/abi.h (NeedsPanicOut, PanicOutType, kPanicOutName)
//   - ultraviolet/src/05_codegen/cleanup.h (ComputeCleanupPlanForCurrentScope, EmitCleanup)
//   - ultraviolet/src/05_codegen/checks.h (LowerPanic, PanicReason)
//   - ultraviolet/src/05_codegen/globals.h (EmitInitPlan)
//   - ultraviolet/src/05_codegen/async_frame.h (kAsyncFrameHeaderSize, kAsyncFrameHeaderAlign)
//   - ultraviolet/src/05_codegen/mangle.h (MangleProc)
//   - ultraviolet/src/04_analysis/layout/layout.h (LowerTypeForLayout, SizeOf, AlignOf)
//   - ultraviolet/src/04_analysis/types/types.h (TypeRef, IsAsyncType, GetAsyncSig)
//   - ultraviolet/src/04_analysis/memory/regions.h (ComputeExprProvenanceMap)
//
// REFACTORING NOTES:
//   1. LowerProc handles both synchronous and asynchronous procedures
//   2. For async procedures (return type is Async<...>):
//      - CollectAsyncIR gathers yield points and local variable slots
//      - Generates wrapper function that allocates frame and initial state
//      - Generates resume function that handles state machine dispatch
//   3. Contract checking (preconditions/postconditions) is inserted based on [[dynamic]] attr
//   4. Cleanup plan computed for parameters to handle fallthrough returns
//   5. Special handling for 'main' procedure to emit initialization plan
//   6. Consider extracting async procedure lowering into separate file
//
// SPEC RULES IMPLEMENTED:
//   - Lower-Proc: Main procedure lowering judgment
//   - Lower-Proc-ContractPre: Precondition check insertion
//   - Lower-Proc-ContractPost: Postcondition check insertion
//   - Contract-Pre-Check: Runtime precondition check
//   - Contract-Post-Check: Runtime postcondition check
//   - Lower-AsyncReturn: Async procedure return wrapping
//
// ASYNC PROCEDURE STRUCTURE:
//   - Wrapper: Allocates frame, copies params to slots, returns @Suspended
//   - Resume: Dispatches on state, executes body, updates state on yield
//   - Frame layout: [header(state, resume_ptr) | slot_0 | slot_1 | ...]
//
// =============================================================================

#include "05_codegen/lower/lower_proc.h"

#include <algorithm>
#include <map>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "04_analysis/generics/monomorphize.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/type_lower.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_stmt.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/intrinsics/async_frame.h"
#include "05_codegen/ir/aggregate_copy_elision.h"
#include "05_codegen/symbols/mangle.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_control_flow.h"

namespace ultraviolet::codegen {

using namespace ast;

namespace {

bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

// Check if procedure returns an async type
bool IsAsyncProc(const analysis::ScopeContext& scope,
                 const analysis::TypeRef& ret_type) {
  return analysis::AsyncSigOf(scope, ret_type).has_value();
}

// Check if [[dynamic]] attribute is present for runtime contract checks
bool HasDynamicContractAttr(const AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kDynamic);
}

bool HasExportAttr(const AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kExport);
}

bool HasHostExportAttr(const AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kHostExport);
}

std::string InternalProcSymbol(const ModulePath& module_path,
                               const ProcedureDecl& decl) {
  return MangleProc(module_path, decl);
}

std::string NormalizeAttrLiteral(std::string text) {
  if (text.size() >= 2 &&
      ((text.front() == '"' && text.back() == '"') ||
       (text.front() == '\'' && text.back() == '\''))) {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

IRInlineMode InlineModeFor(const AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != analysis::attrs::kInline) {
      continue;
    }
    if (attr.args.empty()) {
      return IRInlineMode::Default;
    }
    for (const auto& arg : attr.args) {
      if (arg.key.has_value() && *arg.key != "kind") {
        continue;
      }
      const auto* token = std::get_if<ast::Token>(&arg.value);
      if (!token) {
        continue;
      }
      const std::string mode = NormalizeAttrLiteral(token->lexeme);
      if (mode == "always") {
        return IRInlineMode::Always;
      }
      if (mode == "never") {
        return IRInlineMode::Never;
      }
      return IRInlineMode::Default;
    }
  }
  return IRInlineMode::Default;
}

analysis::ScopeContext ScopeForModule(const ModulePath& module_path,
                                      const LowerCtx& ctx) {
  analysis::ScopeContext scope;
  if (!ctx.sigma) {
    return scope;
  }
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = module_path;
  scope.target_profile = ctx.target_profile;
  scope.expr_types = ctx.expr_types;
  scope.dynamic_refine_checks = ctx.dynamic_refine_checks;
  scope.generic_call_substs = ctx.generic_call_substs;
  scope.selected_call_targets = ctx.selected_call_targets;
  return scope;
}

bool IsContractParamSnapshotType(const analysis::TypeRef& type) {
  analysis::TypeRef stripped = type;
  while (stripped) {
    const auto* perm = std::get_if<analysis::TypePerm>(&stripped->node);
    if (!perm) {
      break;
    }
    stripped = perm->base;
  }
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<analysis::TypePrim>(&stripped->node);
  if (!prim) {
    return false;
  }
  return prim->name != "()" && prim->name != "!";
}

IRPtr EmitContractParamEntrySnapshots(const ProcedureDecl& decl, LowerCtx& ctx) {
  if (!ctx.active_contract_postcondition) {
    return nullptr;
  }

  std::vector<IRPtr> ir_parts;
  for (const auto& param : decl.params) {
    if (param.mode.has_value()) {
      continue;
    }
    const BindingState* state = ctx.GetBindingState(param.name);
    if (!state || !IsContractParamSnapshotType(state->type)) {
      continue;
    }

    const std::string snapshot_name =
        ctx.FreshTempValue("contract_param_entry").name;

    IRValue source;
    source.kind = IRValue::Kind::Local;
    source.name = param.name;
    ctx.RegisterValueType(source, state->type);

    IRBindVar bind;
    bind.name = snapshot_name;
    bind.value = source;
    bind.type = state->type;

    ctx.RegisterVar(snapshot_name,
                    state->type,
                    false,
                    true,
                    analysis::ProvenanceKind::Param);
    bind.stable_name = ctx.StableBindingName(snapshot_name);

    IRValue snapshot;
    snapshot.kind = IRValue::Kind::Local;
    snapshot.name = snapshot_name;
    ctx.RegisterValueType(snapshot, state->type);

    ctx.contract_param_entry_values[param.name] = snapshot;
    ir_parts.push_back(MakeIR(std::move(bind)));
  }

  if (ir_parts.empty()) {
    return nullptr;
  }
  return SeqIR(std::move(ir_parts));
}

LowerCtx::ExportUnwindMode ExportUnwindModeFor(const AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != "unwind") {
      continue;
    }
    const auto mode_tok = ast::get_attr_token_arg(attr, 0);
    if (!mode_tok.has_value()) {
      return LowerCtx::ExportUnwindMode::Abort;
    }
    std::string mode = mode_tok->lexeme;
    if (mode.size() >= 2 &&
        ((mode.front() == '"' && mode.back() == '"') ||
         (mode.front() == '\'' && mode.back() == '\''))) {
      mode = mode.substr(1, mode.size() - 2);
    }
    if (mode == "catch") {
      return LowerCtx::ExportUnwindMode::Catch;
    }
    return LowerCtx::ExportUnwindMode::Abort;
  }
  return LowerCtx::ExportUnwindMode::Abort;
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align) {
  if (align == 0) {
    return value;
  }
  const std::uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

struct AsyncIRInfo {
  std::size_t next_state = 1;
  std::unordered_map<std::string, analysis::TypeRef> slot_types;
  std::unordered_map<std::string, std::vector<std::string>> slot_aliases;
  std::vector<std::string> slot_order;
};

struct ContractImplicationSplit {
  ast::ExprPtr pre;
  ast::ExprPtr post;
};

ast::ExprPtr RebuildBinaryExpr(const ast::ExprPtr& seed,
                               const std::string& op,
                               const ast::ExprPtr& lhs,
                               const ast::ExprPtr& rhs) {
  if (!lhs || !rhs) {
    return nullptr;
  }
  ast::BinaryExpr bin;
  bin.op = op;
  bin.lhs = lhs;
  bin.rhs = rhs;
  ast::Expr rebuilt;
  rebuilt.span = seed ? seed->span : core::Span{};
  rebuilt.node = std::move(bin);
  return std::make_shared<ast::Expr>(std::move(rebuilt));
}

std::optional<ContractImplicationSplit> SplitContractImplicationExpr(
    const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }

  if (const auto* pipe = std::get_if<ast::PipelineExpr>(&expr->node)) {
    if (pipe->lhs && pipe->rhs) {
      return ContractImplicationSplit{pipe->lhs, pipe->rhs};
    }
    return std::nullopt;
  }

  const auto* bin = std::get_if<ast::BinaryExpr>(&expr->node);
  if (!bin || !bin->lhs || !bin->rhs) {
    return std::nullopt;
  }

  if (bin->op == "=>") {
    return ContractImplicationSplit{bin->lhs, bin->rhs};
  }

  if (const auto rhs_split = SplitContractImplicationExpr(bin->rhs)) {
    ast::ExprPtr rebuilt_pre =
        RebuildBinaryExpr(expr, bin->op, bin->lhs, rhs_split->pre);
    if (rebuilt_pre && rhs_split->post) {
      return ContractImplicationSplit{rebuilt_pre, rhs_split->post};
    }
    return std::nullopt;
  }

  if (const auto lhs_split = SplitContractImplicationExpr(bin->lhs)) {
    ast::ExprPtr rebuilt_post =
        RebuildBinaryExpr(expr, bin->op, lhs_split->post, bin->rhs);
    if (lhs_split->pre && rebuilt_post) {
      return ContractImplicationSplit{lhs_split->pre, rebuilt_post};
    }
    return std::nullopt;
  }

  return std::nullopt;
}

struct EntryExprCollector {
  std::vector<const ast::EntryExpr*> order;
  std::unordered_set<const ast::EntryExpr*> seen;

  void Visit(const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            if (seen.insert(&node).second) {
              order.push_back(&node);
            }
            Visit(node.expr);
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            Visit(node.expr);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            Visit(node.lhs);
            Visit(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              Visit(elem);
            }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& subexpr) {
            Visit(subexpr);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          Visit(node.value);
          Visit(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              Visit(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (node.payload_opt) {
              std::visit(
                  [&](const auto& payload) {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                      for (const auto& elem : payload.elements) {
                        Visit(elem);
                      }
                    } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                      for (const auto& field : payload.fields) {
                        Visit(field.value);
                      }
                    }
                  },
                  *node.payload_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            Visit(node.base);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            Visit(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            Visit(node.base);
            Visit(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            Visit(node.callee);
            for (const auto& arg : node.args) {
              Visit(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            Visit(node.receiver);
            for (const auto& arg : node.args) {
              Visit(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            Visit(node.cond);
            Visit(node.then_expr);
            Visit(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            Visit(node.scrutinee);
            for (const auto& case_clause : node.cases) {
              Visit(case_clause.body);
            }
            Visit(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            Visit(node.scrutinee);
            Visit(node.then_expr);
            Visit(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                std::visit(
                    [&](const auto& stmt_node) {
                      using ST = std::decay_t<decltype(stmt_node)>;
                      if constexpr (std::is_same_v<ST, ast::ExprStmt>) {
                        Visit(stmt_node.value);
                      } else if constexpr (std::is_same_v<ST, ast::ReturnStmt>) {
                        Visit(stmt_node.value_opt);
                      } else if constexpr (std::is_same_v<ST, ast::BreakStmt>) {
                        Visit(stmt_node.value_opt);
                      }
                    },
                    stmt);
              }
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            Visit(node.cond);
            if (node.body) {
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            Visit(node.iter);
            if (node.body) {
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
            if (node.block) {
              for (const auto& stmt : node.block->stmts) {
                std::visit(
                    [&](const auto& stmt_node) {
                      using ST = std::decay_t<decltype(stmt_node)>;
                      if constexpr (std::is_same_v<ST, ast::ExprStmt>) {
                        Visit(stmt_node.value);
                      } else if constexpr (std::is_same_v<ST, ast::ReturnStmt>) {
                        Visit(stmt_node.value_opt);
                      } else if constexpr (std::is_same_v<ST, ast::BreakStmt>) {
                        Visit(stmt_node.value_opt);
                      }
                    },
                    stmt);
              }
              Visit(node.block->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              Visit(node.block->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
            Visit(node.body);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            Visit(node.place);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            Visit(node.place);
          } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            Visit(node.lhs);
            Visit(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            Visit(node.domain);
            if (node.body) {
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            if (node.body) {
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            Visit(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            Visit(node.range);
            if (node.body) {
              Visit(node.body->tail_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
            Visit(node.value);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              Visit(arm.expr);
              Visit(arm.handler.value);
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& e : node.exprs) {
              Visit(e);
            }
          }
        },
        expr->node);
  }
};

IRPtr EmitEntryCapturesForPostcondition(const ast::ExprPtr& postcond, LowerCtx& ctx) {
  if (!postcond) {
    return nullptr;
  }

  EntryExprCollector collector;
  collector.Visit(postcond);
  if (collector.order.empty()) {
    return nullptr;
  }

  std::vector<IRPtr> parts;
  std::size_t capture_index = 0;
  for (const auto* entry_expr : collector.order) {
    if (!entry_expr || !entry_expr->expr) {
      continue;
    }

    auto capture_result = LowerExpr(*entry_expr->expr, ctx);
    parts.push_back(capture_result.ir);

    analysis::TypeRef capture_type = ctx.LookupValueType(capture_result.value);
    if (!capture_type && ctx.expr_type) {
      capture_type = ctx.expr_type(*entry_expr->expr);
    }

    const std::string capture_name =
        "__uv_entry_capture_" + std::to_string(capture_index++);
    IRBindVar bind;
    bind.name = capture_name;
    bind.value = capture_result.value;
    bind.type = capture_type;
    bind.prov = analysis::ProvenanceKind::Bottom;

    ctx.RegisterVar(capture_name,
                    capture_type,
                    /*has_responsibility=*/false,
                    /*is_immovable=*/false,
                    analysis::ProvenanceKind::Bottom);
    bind.stable_name = ctx.StableBindingName(capture_name);
    parts.push_back(MakeIR(std::move(bind)));
    IRValue captured_value;
    captured_value.kind = IRValue::Kind::Local;
    captured_value.name = capture_name;
    if (capture_type) {
      ctx.RegisterValueType(captured_value, capture_type);
    }
    ctx.contract_entry_values[entry_expr] = captured_value;
  }

  if (parts.empty()) {
    return nullptr;
  }
  return SeqIR(std::move(parts));
}

void AddAsyncSlot(AsyncIRInfo& info,
                  const std::string& name,
                  const analysis::TypeRef& type,
                  const std::vector<std::string>& aliases = {}) {
  if (!type) {
    return;
  }
  if (info.slot_types.emplace(name, type).second) {
    info.slot_order.push_back(name);
  }
  auto& known_aliases = info.slot_aliases[name];
  for (const auto& alias : aliases) {
    if (alias.empty() || alias == name) {
      continue;
    }
    if (std::find(known_aliases.begin(), known_aliases.end(), alias) ==
        known_aliases.end()) {
      known_aliases.push_back(alias);
    }
  }
}

void AddLocalUse(const IRValue& value, std::unordered_set<std::string>& uses) {
  if (value.kind == IRValue::Kind::Local && !value.name.empty()) {
    uses.insert(value.name);
  }
}

void AddLocalNameUse(const std::string& name, std::unordered_set<std::string>& uses) {
  if (!name.empty()) {
    uses.insert(name);
  }
}

std::string StableOrSourceName(const std::string& source_name,
                               const std::string& stable_name) {
  return stable_name.empty() ? source_name : stable_name;
}

bool CollectUsesAfterSuspension(IRPtr& ir,
                                bool seen_suspension,
                                std::unordered_set<std::string>& uses,
                                AsyncIRInfo& info) {
  if (!ir) {
    return seen_suspension;
  }
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IRYield>) {
          if (seen_suspension) {
            AddLocalUse(node.value, uses);
            AddLocalUse(node.keys_record, uses);
          }
          node.state_index = info.next_state++;
          return true;
        } else if constexpr (std::is_same_v<T, IRYieldFrom>) {
          // The delegated async state must survive the suspension created by
          // this yield-from itself so the resume path continues the same inner
          // async value instead of recreating it.
          AddLocalUse(node.source, uses);
          node.state_index = info.next_state++;
          return true;
        } else if constexpr (std::is_same_v<T, IRSeq>) {
          bool seen_here = seen_suspension;
          for (auto& item : node.items) {
            seen_here = CollectUsesAfterSuspension(item, seen_here, uses, info);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRIf>) {
          if (seen_suspension) {
            AddLocalUse(node.cond, uses);
          }
          const bool then_seen =
              CollectUsesAfterSuspension(node.then_ir, seen_suspension, uses, info);
          const bool else_seen =
              CollectUsesAfterSuspension(node.else_ir, seen_suspension, uses, info);
          return seen_suspension || then_seen || else_seen;
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          bool seen_here =
              CollectUsesAfterSuspension(node.setup, seen_suspension, uses, info);
          seen_here = CollectUsesAfterSuspension(node.body, seen_here, uses, info);
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          bool seen_here =
              CollectUsesAfterSuspension(node.iter_ir, seen_suspension, uses, info);
          if (seen_here && node.iter_value.has_value()) {
            AddLocalUse(*node.iter_value, uses);
          }
          seen_here = CollectUsesAfterSuspension(node.cond_ir, seen_here, uses, info);
          if (seen_here && node.cond_value.has_value()) {
            AddLocalUse(*node.cond_value, uses);
          }
          seen_here = CollectUsesAfterSuspension(node.body_ir, seen_here, uses, info);
          if (seen_here) {
            AddLocalUse(node.body_value, uses);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          if (seen_suspension) {
            AddLocalUse(node.scrutinee, uses);
          }
          bool seen_here = seen_suspension;
          for (auto& arm : node.arms) {
            seen_here = CollectUsesAfterSuspension(arm.body, seen_here, uses, info);
            seen_here = CollectUsesAfterSuspension(arm.cleanup_ir, seen_here, uses, info);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          if (seen_suspension) {
            AddLocalUse(node.owner, uses);
          }
          return CollectUsesAfterSuspension(node.body, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          if (seen_suspension && node.region.has_value()) {
            AddLocalUse(*node.region, uses);
          }
          return CollectUsesAfterSuspension(node.body, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRPanicCheck>) {
          return seen_suspension;
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          return CollectUsesAfterSuspension(node.cleanup_ir, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
          return CollectUsesAfterSuspension(node.cleanup_ir, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRInitPanicRaise>) {
          return CollectUsesAfterSuspension(node.cleanup_ir, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRCheckPoison>) {
          return seen_suspension;
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          return CollectUsesAfterSuspension(node.cleanup_ir, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          if (seen_suspension) {
            AddLocalUse(node.domain, uses);
            if (node.cancel_token.has_value()) {
              AddLocalUse(*node.cancel_token, uses);
            }
          }
          return CollectUsesAfterSuspension(node.body, seen_suspension, uses, info);
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          bool seen_here =
              CollectUsesAfterSuspension(node.captured_env, seen_suspension, uses, info);
          seen_here = CollectUsesAfterSuspension(node.body, seen_here, uses, info);
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          bool seen_here =
              CollectUsesAfterSuspension(node.captured_env, seen_suspension, uses, info);
          seen_here = CollectUsesAfterSuspension(node.body, seen_here, uses, info);
          if (seen_here) {
            AddLocalUse(node.range, uses);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          bool seen_here = seen_suspension;
          for (auto& arm : node.arms) {
            seen_here = CollectUsesAfterSuspension(arm.async_ir, seen_here, uses, info);
            seen_here = CollectUsesAfterSuspension(arm.handler_ir, seen_here, uses, info);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          bool seen_here = seen_suspension;
          for (auto& arm : node.arms) {
            seen_here = CollectUsesAfterSuspension(arm.async_ir, seen_here, uses, info);
            seen_here = CollectUsesAfterSuspension(arm.handler_ir, seen_here, uses, info);
          }
          return seen_here;
        } else if constexpr (std::is_same_v<T, IRAll>) {
          bool seen_here = seen_suspension;
          for (auto& item : node.async_irs) {
            seen_here = CollectUsesAfterSuspension(item, seen_here, uses, info);
          }
          return seen_here;
        } else {
          if (!seen_suspension) {
            return false;
          }
          if constexpr (std::is_same_v<T, IRCall>) {
            AddLocalUse(node.callee, uses);
            for (const auto& arg : node.args) {
              AddLocalUse(arg, uses);
            }
          } else if constexpr (std::is_same_v<T, IRReadVar>) {
            AddLocalNameUse(node.name, uses);
          } else if constexpr (std::is_same_v<T, IRCallVTable>) {
            AddLocalUse(node.base, uses);
            for (const auto& arg : node.args) {
              AddLocalUse(arg, uses);
            }
          } else if constexpr (std::is_same_v<T, IRBindVar>) {
            AddLocalUse(node.value, uses);
          } else if constexpr (std::is_same_v<T, IRStoreVar> ||
                               std::is_same_v<T, IRStoreVarNoDrop> ||
                               std::is_same_v<T, IRReturn> ||
                               std::is_same_v<T, IRResult>) {
            AddLocalUse(node.value, uses);
          } else if constexpr (std::is_same_v<T, IRReadPtr>) {
            AddLocalUse(node.ptr, uses);
          } else if constexpr (std::is_same_v<T, IRWritePtr>) {
            AddLocalUse(node.ptr, uses);
            AddLocalUse(node.value, uses);
          } else if constexpr (std::is_same_v<T, IRUnaryOp>) {
            AddLocalUse(node.operand, uses);
          } else if constexpr (std::is_same_v<T, IRBinaryOp>) {
            AddLocalUse(node.lhs, uses);
            AddLocalUse(node.rhs, uses);
          } else if constexpr (std::is_same_v<T, IRCast> ||
                               std::is_same_v<T, IRTransmute> ||
                               std::is_same_v<T, IRCheckCast>) {
            AddLocalUse(node.value, uses);
          } else if constexpr (std::is_same_v<T, IRCheckIndex>) {
            AddLocalUse(node.base, uses);
            AddLocalUse(node.index, uses);
          } else if constexpr (std::is_same_v<T, IRCheckRange>) {
            AddLocalUse(node.base, uses);
            if (node.range_value.has_value()) {
              AddLocalUse(*node.range_value, uses);
            }
          } else if constexpr (std::is_same_v<T, IRCheckSliceLen>) {
            AddLocalUse(node.base, uses);
            AddLocalUse(node.value, uses);
            if (node.range_value.has_value()) {
              AddLocalUse(*node.range_value, uses);
            }
          } else if constexpr (std::is_same_v<T, IRCheckOp>) {
            AddLocalUse(node.lhs, uses);
            if (node.rhs.has_value()) {
              AddLocalUse(*node.rhs, uses);
            }
          } else if constexpr (std::is_same_v<T, IRAlloc>) {
            if (node.region.has_value()) {
              AddLocalUse(*node.region, uses);
            }
            AddLocalUse(node.value, uses);
          } else if constexpr (std::is_same_v<T, IRBreak>) {
            if (node.value.has_value()) {
              AddLocalUse(*node.value, uses);
            }
          } else if constexpr (std::is_same_v<T, IRMoveState>) {
            (void)node;
          } else if constexpr (std::is_same_v<T, IRBranch>) {
            if (node.cond.has_value()) {
              AddLocalUse(*node.cond, uses);
            }
          } else if constexpr (std::is_same_v<T, IRPhi>) {
            for (const auto& incoming : node.incoming) {
              AddLocalUse(incoming.value, uses);
            }
          } else if constexpr (std::is_same_v<T, IRWait>) {
            AddLocalUse(node.handle, uses);
          } else if constexpr (std::is_same_v<T, IRSync>) {
            AddLocalUse(node.async_value, uses);
          } else if constexpr (std::is_same_v<T, IRAsyncComplete> ||
                               std::is_same_v<T, IRAsyncFail>) {
            AddLocalUse(node.value, uses);
          }
          return true;
        }
      },
      ir->node);
}

void CollectAsyncSlotsFromBindings(const IRPtr& ir,
                                   AsyncIRInfo& info,
                                   const std::unordered_set<std::string>& uses_after_suspension) {
  if (!ir) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IRBindVar>) {
          const std::string slot_name =
              StableOrSourceName(node.name, node.stable_name);
          if (uses_after_suspension.find(node.name) != uses_after_suspension.end() ||
              uses_after_suspension.find(slot_name) != uses_after_suspension.end()) {
            AddAsyncSlot(info, slot_name, node.type, {node.name});
          }
        } else if constexpr (std::is_same_v<T, IRSeq>) {
          for (const auto& item : node.items) {
            CollectAsyncSlotsFromBindings(item, info, uses_after_suspension);
          }
        } else if constexpr (std::is_same_v<T, IRIf>) {
          CollectAsyncSlotsFromBindings(node.then_ir, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.else_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          CollectAsyncSlotsFromBindings(node.setup, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          CollectAsyncSlotsFromBindings(node.iter_ir, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.cond_ir, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.body_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          for (const auto& arm : node.arms) {
            CollectAsyncSlotsFromBindings(arm.body, info, uses_after_suspension);
            CollectAsyncSlotsFromBindings(arm.cleanup_ir, info, uses_after_suspension);
          }
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRPanicCheck>) {
          return;
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          CollectAsyncSlotsFromBindings(node.cleanup_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
          CollectAsyncSlotsFromBindings(node.cleanup_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRInitPanicRaise>) {
          CollectAsyncSlotsFromBindings(node.cleanup_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRCheckPoison>) {
          return;
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          CollectAsyncSlotsFromBindings(node.cleanup_ir, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          CollectAsyncSlotsFromBindings(node.captured_env, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          CollectAsyncSlotsFromBindings(node.captured_env, info, uses_after_suspension);
          CollectAsyncSlotsFromBindings(node.body, info, uses_after_suspension);
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          for (const auto& arm : node.arms) {
            CollectAsyncSlotsFromBindings(arm.async_ir, info, uses_after_suspension);
            CollectAsyncSlotsFromBindings(arm.handler_ir, info, uses_after_suspension);
          }
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          for (const auto& arm : node.arms) {
            CollectAsyncSlotsFromBindings(arm.async_ir, info, uses_after_suspension);
            CollectAsyncSlotsFromBindings(arm.handler_ir, info, uses_after_suspension);
          }
        } else if constexpr (std::is_same_v<T, IRAll>) {
          for (const auto& item : node.async_irs) {
            CollectAsyncSlotsFromBindings(item, info, uses_after_suspension);
          }
        }
      },
      ir->node);
}

analysis::TypeRef InstantiateTypeRef(const analysis::TypeRef& type,
                                     const std::map<std::string, analysis::TypeRef>& type_subst) {
  if (!type) {
    return type;
  }
  return analysis::InstantiateType(type, type_subst);
}

void InstantiateIRTypes(IRPtr& ir,
                        const std::map<std::string, analysis::TypeRef>& type_subst) {
  if (!ir) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IRSeq>) {
          for (auto& item : node.items) {
            InstantiateIRTypes(item, type_subst);
          }
        } else if constexpr (std::is_same_v<T, IRBindVar>) {
          node.type = InstantiateTypeRef(node.type, type_subst);
        } else if constexpr (std::is_same_v<T, IRCast>) {
          node.target = InstantiateTypeRef(node.target, type_subst);
        } else if constexpr (std::is_same_v<T, IRTransmute>) {
          node.from = InstantiateTypeRef(node.from, type_subst);
          node.to = InstantiateTypeRef(node.to, type_subst);
        } else if constexpr (std::is_same_v<T, IRCheckCast>) {
          node.target = InstantiateTypeRef(node.target, type_subst);
        } else if constexpr (std::is_same_v<T, IRAlloc>) {
          node.type = InstantiateTypeRef(node.type, type_subst);
        } else if constexpr (std::is_same_v<T, IRIf>) {
          InstantiateIRTypes(node.then_ir, type_subst);
          InstantiateIRTypes(node.else_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          InstantiateIRTypes(node.setup, type_subst);
          InstantiateIRTypes(node.body, type_subst);
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          InstantiateIRTypes(node.iter_ir, type_subst);
          InstantiateIRTypes(node.cond_ir, type_subst);
          InstantiateIRTypes(node.body_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          node.scrutinee_type = InstantiateTypeRef(node.scrutinee_type, type_subst);
          for (auto& arm : node.arms) {
            InstantiateIRTypes(arm.body, type_subst);
            InstantiateIRTypes(arm.cleanup_ir, type_subst);
            arm.value_type = InstantiateTypeRef(arm.value_type, type_subst);
          }
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          InstantiateIRTypes(node.body, type_subst);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          InstantiateIRTypes(node.body, type_subst);
        } else if constexpr (std::is_same_v<T, IRPhi>) {
          node.type = InstantiateTypeRef(node.type, type_subst);
        } else if constexpr (std::is_same_v<T, IRPanicCheck>) {
          return;
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          InstantiateIRTypes(node.cleanup_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
          InstantiateIRTypes(node.cleanup_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRInitPanicRaise>) {
          InstantiateIRTypes(node.cleanup_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRCheckPoison>) {
          return;
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          InstantiateIRTypes(node.cleanup_ir, type_subst);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          InstantiateIRTypes(node.body, type_subst);
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          InstantiateIRTypes(node.captured_env, type_subst);
          InstantiateIRTypes(node.body, type_subst);
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          InstantiateIRTypes(node.body, type_subst);
          InstantiateIRTypes(node.captured_env, type_subst);
        } else if constexpr (std::is_same_v<T, IRYieldFrom>) {
          node.source_type = InstantiateTypeRef(node.source_type, type_subst);
        } else if constexpr (std::is_same_v<T, IRSync>) {
          node.async_type = InstantiateTypeRef(node.async_type, type_subst);
          node.result_type = InstantiateTypeRef(node.result_type, type_subst);
          node.error_type = InstantiateTypeRef(node.error_type, type_subst);
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          node.result_type = InstantiateTypeRef(node.result_type, type_subst);
          for (auto& arm : node.arms) {
            InstantiateIRTypes(arm.async_ir, type_subst);
            InstantiateIRTypes(arm.handler_ir, type_subst);
          }
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          node.stream_type = InstantiateTypeRef(node.stream_type, type_subst);
          for (auto& arm : node.arms) {
            InstantiateIRTypes(arm.async_ir, type_subst);
            InstantiateIRTypes(arm.handler_ir, type_subst);
          }
        } else if constexpr (std::is_same_v<T, IRAll>) {
          node.tuple_type = InstantiateTypeRef(node.tuple_type, type_subst);
          for (auto& err_type : node.error_types) {
            err_type = InstantiateTypeRef(err_type, type_subst);
          }
          for (auto& async_ir : node.async_irs) {
            InstantiateIRTypes(async_ir, type_subst);
          }
        } else if constexpr (std::is_same_v<T, IRAsyncComplete>) {
          node.async_type = InstantiateTypeRef(node.async_type, type_subst);
          node.result_type = InstantiateTypeRef(node.result_type, type_subst);
        } else if constexpr (std::is_same_v<T, IRAsyncFail>) {
          node.async_type = InstantiateTypeRef(node.async_type, type_subst);
          node.error_type = InstantiateTypeRef(node.error_type, type_subst);
        }
      },
      ir->node);
}

// Emit precondition check: if (!pre) panic(ContractPre)
IRPtr EmitPreconditionCheck(const ExprPtr& precond, LowerCtx& ctx) {
  if (!precond) {
    return nullptr;
  }
  SPEC_RULE("Contract-Pre-Check");

  auto pre_result = LowerExpr(*precond, ctx);

  // Emit: if (!precond) panic(ContractPre)
  IRIf check;
  check.cond = pre_result.value;
  check.then_ir = nullptr;  // Pass if true
  check.else_ir = LowerContractViolation(ContractKind::Pre,
                                         ctx,
                                         precond.get(),
                                         precond->span);  // Panic if false
  check.result = ctx.FreshTempValue("pre_check");

  return SeqIR({pre_result.ir, MakeIR(std::move(check))});
}

}  // namespace

ProcIR LowerProc(const ProcedureDecl& decl,
                 const ModulePath& module_path,
                 LowerCtx& ctx,
                 std::optional<std::string> symbol_override) {
  SPEC_RULE("Lower-Proc");

  ProcIR ir;
  const bool debug_proc = core::IsDebugEnabled("codegen");

  // Mangle symbol name
  ir.symbol = symbol_override.value_or(InternalProcSymbol(module_path, decl));
  ir.defining_module_path = module_path;
  ir.inline_mode = InlineModeFor(decl.attrs);
  ir.cold = analysis::HasAttribute(decl.attrs, analysis::attrs::kCold);
  ctx.module_path = module_path;
  const auto prev_proc_ret_type = ctx.proc_ret_type;
  const auto prev_current_proc_symbol = ctx.current_proc_symbol;
  const std::uint64_t prev_current_closure_counter = ctx.current_closure_counter;
  const std::optional<std::string> seeded_proc_symbol = ctx.current_proc_symbol;
  ctx.current_proc_symbol = seeded_proc_symbol.value_or(ir.symbol);
  ctx.current_closure_counter = 0;
  auto log_stage = [&](std::string_view stage) {
    if (!debug_proc) {
      return;
    }
    std::cerr << "[lower-proc-debug] symbol=" << ir.symbol
              << " stage=" << stage << "\n";
  };
  log_stage("start");

  // Prepare scope context for type lowering
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);

  ctx.expr_prov.reset();
  ctx.expr_region.reset();
  ctx.expr_region_tags.reset();
  if (ctx.sigma && decl.body) {
    auto prov =
        analysis::ComputeExprProvenanceMap(scope, module_path, decl.params,
                                           decl.body, std::nullopt);
    if (prov.ok) {
      ctx.expr_prov = std::make_shared<
          std::unordered_map<const ast::Expr*, analysis::ProvenanceKind>>(
          std::move(prov.expr_prov));
      ctx.expr_region = std::make_shared<
          std::unordered_map<const ast::Expr*, std::string>>(
          std::move(prov.expr_region_targets));
      ctx.expr_region_tags = std::make_shared<
          std::unordered_map<const ast::Expr*, std::string>>(
          std::move(prov.expr_region_tags));
    }
  }
  log_stage("provenance-ready");
  // Establish function root scope for parameters and cleanup
  ctx.PushScope(false, false);

  // Lower parameters
  for (const auto& param : decl.params) {
    IRParam p;
    p.mode = param.mode.has_value() ? std::optional<analysis::ParamMode>(analysis::ParamMode::Move)
                                    : std::nullopt;
    p.name = param.name;
    if (param.type && ctx.sigma) {
      const auto lowered = analysis::LowerType(scope, param.type);
      if (lowered.ok && lowered.type) {
        p.type = lowered.type;
      }
    }
    const bool has_resp = param.mode.has_value();
    const bool preserve_addr_provenance = !param.mode.has_value();
    ctx.RegisterVar(param.name, p.type, has_resp, false,
                    analysis::ProvenanceKind::Param,
                    std::nullopt,
                    preserve_addr_provenance);
    p.stable_name = ctx.StableBindingName(param.name);
    ir.params.push_back(p);
  }
  // Lower return type
  if (decl.return_type_opt && ctx.sigma) {
    const auto lowered = analysis::LowerType(scope, decl.return_type_opt);
    if (lowered.ok && lowered.type) {
      ir.ret = lowered.type;
    }
  }
  if (!ir.ret) {
    ctx.ReportCodegenFailure();
    ctx.PopScope();
    ctx.proc_ret_type = prev_proc_ret_type;
    ctx.current_proc_symbol = prev_current_proc_symbol;
    ctx.current_closure_counter = prev_current_closure_counter;
    return ir;
  }
  log_stage("signature-ready");
  // Set proc_ret_type for async procedure detection in return statement lowering
  ctx.proc_ret_type = ir.ret;

  const bool is_export_proc = HasExportAttr(decl.attrs);
  if (is_export_proc) {
    for (auto& param : ir.params) {
      param.mode = analysis::ParamMode::Move;
    }
    if (const auto abi = analysis::GetAttributeValue(decl.attrs, analysis::attrs::kExport);
        abi.has_value()) {
      ir.abi = NormalizeAttrLiteral(*abi);
    }
    ctx.RegisterExportUnwindMode(ir.symbol, ExportUnwindModeFor(decl.attrs));
  }

  // Exported procedures are foreign-callable boundaries and MUST keep the
  // declared ABI surface. Do not append an internal panic out-parameter.
  if (!is_export_proc && ctx.NeedsPanicOutForSymbol(ir.symbol)) {
    ir.params.push_back(PanicOutParam());
  }
  const bool prev_dynamic_checks = ctx.dynamic_checks;
  const ast::Expr* prev_active_postcondition = ctx.active_contract_postcondition;
  const auto prev_contract_result_value = ctx.contract_result_value;
  const auto prev_contract_entry_values = ctx.contract_entry_values;
  const auto prev_contract_param_entry_values = ctx.contract_param_entry_values;
  const bool prev_lowering_contract_postcondition =
      ctx.lowering_contract_postcondition;
  ctx.dynamic_checks = HasDynamicContractAttr(decl.attrs);
  ctx.active_contract_postcondition = nullptr;
  ctx.contract_result_value.reset();
  ctx.contract_entry_values.clear();
  ctx.contract_param_entry_values.clear();
  ctx.lowering_contract_postcondition = false;

  // Check for dynamic contract checks
  const bool has_dynamic_contract = HasDynamicContractAttr(decl.attrs) &&
                                    decl.contract.has_value();
  ast::ExprPtr contract_pre =
      (has_dynamic_contract && decl.contract) ? decl.contract->precondition
                                              : nullptr;
  ast::ExprPtr contract_post =
      (has_dynamic_contract && decl.contract) ? decl.contract->postcondition
                                              : nullptr;

  if (has_dynamic_contract && contract_pre && !contract_post) {
    if (const auto split = SplitContractImplicationExpr(contract_pre)) {
      contract_pre = split->pre;
      contract_post = split->post;
    }
  }
  ctx.active_contract_postcondition = contract_post ? contract_post.get() : nullptr;

  // Emit precondition check if [[dynamic]] contract present
  IRPtr precond_ir = nullptr;
  if (has_dynamic_contract && contract_pre) {
    SPEC_RULE("Lower-Proc-ContractPre");
    precond_ir = EmitPreconditionCheck(contract_pre, ctx);
  }

  // Capture all @entry(...) values once per invocation before body execution.
  IRPtr entry_capture_ir = nullptr;
  if (has_dynamic_contract && contract_post) {
    entry_capture_ir = EmitEntryCapturesForPostcondition(contract_post, ctx);
  }
  IRPtr param_entry_snapshot_ir = nullptr;
  if (has_dynamic_contract && contract_post) {
    param_entry_snapshot_ir = EmitContractParamEntrySnapshots(decl, ctx);
  }

  // Lower body
  log_stage("lower-body-start");
  auto body_res = LowerBlock(*decl.body, ctx);
  log_stage("lower-body-finish");

  // Cleanup for parameters on fallthrough
  IRPtr scope_enter_ir = EmptyIR();
  ctx.RegisterRuntimeScopeExitIfRequired();
  if (ctx.CurrentScopeRequiresRuntime()) {
    if (const auto scope_id = ctx.CurrentRuntimeScopeId()) {
      scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, ctx);
    }
  }
  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
  ctx.PopScope();
  log_stage("cleanup-ready");

  std::vector<IRPtr> body_seq;
  body_seq.push_back(scope_enter_ir);

  // Add precondition check at procedure entry
  if (param_entry_snapshot_ir) {
    body_seq.push_back(param_entry_snapshot_ir);
  }
  if (entry_capture_ir) {
    body_seq.push_back(entry_capture_ir);
  }
  if (precond_ir) {
    body_seq.push_back(precond_ir);
  }

  if (body_res.ir) {
    body_seq.push_back(body_res.ir);
  }
  const bool body_may_fallthrough = IRFlowMayFallThrough(body_res.ir);
  if (cleanup_ir && body_may_fallthrough) {
    body_seq.push_back(cleanup_ir);
  }

  const bool has_tail = decl.body && decl.body->tail_opt;
  const bool ret_is_unit = IsUnitType(ir.ret);
  if (has_tail || (body_may_fallthrough && ret_is_unit)) {
    if (has_dynamic_contract && contract_post) {
      SPEC_RULE("Lower-Proc-ContractPost");
      if (IRPtr postcheck_ir =
              EmitDynamicPostconditionCheckForReturn(body_res.value, ctx)) {
        body_seq.push_back(postcheck_ir);
      }
    }
    IRReturn ret;
    ret.value = body_res.value;
    body_seq.push_back(MakeIR(std::move(ret)));
  }

  ir.body = SeqIR(std::move(body_seq));

  if (!ir.abi.has_value()) {
    ir.aggregate_copy_elision = AnalyzeAggregateCopyElision(ir, ctx);
  }

  if (IsAsyncProc(scope, ir.ret)) {
    log_stage("async-lower-start");
    const auto sig = analysis::AsyncSigOf(scope, ir.ret);
    if (sig.has_value()) {
      AsyncIRInfo async_ir;
      std::unordered_set<std::string> uses_after_suspension;
      CollectUsesAfterSuspension(ir.body, false, uses_after_suspension, async_ir);
      std::vector<std::string> param_names;
      for (const auto& param : ir.params) {
        if (param.name == kPanicOutName) {
          continue;
        }
        param_names.push_back(param.name);
        const std::string slot_name =
            StableOrSourceName(param.name, param.stable_name);
        if (uses_after_suspension.find(param.name) != uses_after_suspension.end() ||
            uses_after_suspension.find(slot_name) != uses_after_suspension.end()) {
          AddAsyncSlot(async_ir, slot_name, param.type, {param.name});
        }
      }
      CollectAsyncSlotsFromBindings(ir.body, async_ir, uses_after_suspension);

      const analysis::ScopeContext& scope_inner = ScopeForLowering(ctx);

      LowerCtx::AsyncProcInfo async_info;
      async_info.async_type = ir.ret;
      async_info.out_type = sig->out;
      async_info.in_type = sig->in;
      async_info.result_type = sig->result;
      async_info.err_type = sig->err;
      async_info.resume_symbol = ir.symbol + "$resume";
      async_info.resume_needs_panic_out =
          ctx.NeedsPanicOutForSymbol(async_info.resume_symbol);
      async_info.param_names = param_names;
      async_info.slot_order = async_ir.slot_order;
      async_info.slot_aliases = async_ir.slot_aliases;

      std::uint64_t offset = kAsyncFrameHeaderSize;
      std::uint64_t frame_align = kAsyncFrameHeaderAlign;
      for (const auto& name : async_ir.slot_order) {
        const auto it = async_ir.slot_types.find(name);
        if (it == async_ir.slot_types.end()) {
          continue;
        }
        const auto& type = it->second;
        const auto size_opt = ::ultraviolet::analysis::layout::SizeOf(scope_inner, type);
        const auto align_opt = ::ultraviolet::analysis::layout::AlignOf(scope_inner, type);
        if (!size_opt.has_value()) {
          ctx.ReportCodegenFailure();
          continue;
        }
        const std::uint64_t size = *size_opt;
        const std::uint64_t align = align_opt.value_or(1);
        offset = AlignUp(offset, align);
        LowerCtx::AsyncFrameSlot slot;
        slot.type = type;
        slot.offset = offset;
        slot.size = size;
        slot.align = align;
        async_info.slots[name] = slot;
        offset += size;
        frame_align = std::max(frame_align, align);
      }
      async_info.frame_align = frame_align;
      async_info.frame_size = AlignUp(offset, frame_align);

      LowerCtx::AsyncProcInfo wrapper_info = async_info;
      wrapper_info.is_wrapper = true;
      wrapper_info.is_resume = false;

      LowerCtx::AsyncProcInfo resume_info = async_info;
      resume_info.is_wrapper = false;
      resume_info.is_resume = true;

      ctx.async_procs[ir.symbol] = std::move(wrapper_info);
      ctx.async_procs[async_info.resume_symbol] = std::move(resume_info);

      ProcIR resume = ir;
      resume.symbol = async_info.resume_symbol;
      resume.params.clear();
      resume.abi = std::string("C");

      resume.params.push_back(HostedEnvParam());
      ctx.hosted_explicit_env_procs.insert(resume.symbol);

      IRParam out_param;
      out_param.mode = analysis::ParamMode::Move;
      out_param.name = std::string(kAsyncOutParamName);
      out_param.stable_name = out_param.name;
      out_param.type = analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, ir.ret);
      resume.params.push_back(std::move(out_param));

      IRParam frame_param;
      frame_param.mode = analysis::ParamMode::Move;
      frame_param.name = "__uv_async_frame";
      frame_param.stable_name = frame_param.name;
      frame_param.type = analysis::MakeTypePtr(analysis::MakeTypePrim("u8"),
                                               analysis::PtrState::Valid);
      resume.params.push_back(frame_param);

      IRParam input_param;
      input_param.mode = analysis::ParamMode::Move;
      input_param.name = "__uv_async_input";
      input_param.stable_name = input_param.name;
      input_param.type = analysis::MakeTypePtr(analysis::MakeTypePrim("u8"),
                                               analysis::PtrState::Valid);
      resume.params.push_back(input_param);

      if (async_info.resume_needs_panic_out) {
        resume.params.push_back(PanicOutParam());
      }

      ctx.QueueExtraProc(std::move(resume), LinkageKind::Internal);
    }
    log_stage("async-lower-finish");
  }

  ctx.dynamic_checks = prev_dynamic_checks;
  ctx.active_contract_postcondition = prev_active_postcondition;
  ctx.contract_result_value = prev_contract_result_value;
  ctx.contract_entry_values = prev_contract_entry_values;
  ctx.contract_param_entry_values = prev_contract_param_entry_values;
  ctx.lowering_contract_postcondition = prev_lowering_contract_postcondition;
  ctx.proc_ret_type = prev_proc_ret_type;
  ctx.current_proc_symbol = prev_current_proc_symbol;
  ctx.current_closure_counter = prev_current_closure_counter;
  log_stage("finish");
  return ir;
}

ProcIR LowerProcInstantiated(const ast::ProcedureDecl& decl,
                             const ast::ModulePath& module_path,
                             const std::string& symbol_override,
                             const std::map<std::string, analysis::TypeRef>& type_subst,
                             LowerCtx& ctx) {
  struct InstantiationCtxSnapshot {
    std::vector<std::string> module_path;
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
    std::optional<std::string> current_proc_symbol;
    std::uint64_t current_closure_counter = 0;
    std::shared_ptr<const std::unordered_map<const ast::Expr*, analysis::ProvenanceKind>>
        expr_prov;
    std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>> expr_region;
    std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>>
        expr_region_tags;
    std::vector<std::string> active_region_aliases;
    bool dynamic_checks = false;
    const ast::Expr* active_contract_postcondition = nullptr;
    std::optional<IRValue> contract_result_value;
    std::unordered_map<const ast::EntryExpr*, IRValue> contract_entry_values;
    std::unordered_map<std::string, IRValue> contract_param_entry_values;
    bool lowering_contract_postcondition = false;
    std::optional<analysis::TypeSubst> active_generic_type_subst;

    explicit InstantiationCtxSnapshot(const LowerCtx& source)
        : module_path(source.module_path),
          scope_stack(source.scope_stack),
          binding_states(source.binding_states),
          derived_values(source.values.derived_values),
          temp_sink(source.temp_sink),
          temp_depth(source.temp_depth),
          suppress_temp_at_depth(source.suppress_temp_at_depth),
          parallel_collect(source.parallel_collect),
          parallel_collect_depth(source.parallel_collect_depth),
          capture_env(source.capture_env),
          proc_ret_type(source.proc_ret_type),
          current_proc_symbol(source.current_proc_symbol),
          current_closure_counter(source.current_closure_counter),
          expr_prov(source.expr_prov),
          expr_region(source.expr_region),
          expr_region_tags(source.expr_region_tags),
          active_region_aliases(source.active_region_aliases),
          dynamic_checks(source.dynamic_checks),
          active_contract_postcondition(source.active_contract_postcondition),
          contract_result_value(source.contract_result_value),
          contract_entry_values(source.contract_entry_values),
          contract_param_entry_values(source.contract_param_entry_values),
          lowering_contract_postcondition(source.lowering_contract_postcondition),
          active_generic_type_subst(source.active_generic_type_subst) {}

    void Restore(LowerCtx& target) const {
      target.module_path = module_path;
      target.scope_stack = scope_stack;
      target.binding_states = binding_states;
      // Keep derived values created while lowering the instantiated procedure.
      // The LLVM emitter consults LowerCtx::derived_values for all procedures,
      // so discarding instantiated entries causes missing-materialization
      // failures for opaque values during codegen.
      auto preserved_derived = std::move(target.values.derived_values);
      target.values.derived_values = derived_values;
      for (auto& [name, info] : preserved_derived) {
        target.values.derived_values.emplace(std::move(name), std::move(info));
      }
      target.temp_sink = temp_sink;
      target.temp_depth = temp_depth;
      target.suppress_temp_at_depth = suppress_temp_at_depth;
      target.parallel_collect = parallel_collect;
      target.parallel_collect_depth = parallel_collect_depth;
      target.capture_env = capture_env;
      target.proc_ret_type = proc_ret_type;
      target.current_proc_symbol = current_proc_symbol;
      target.current_closure_counter = current_closure_counter;
      target.expr_prov = expr_prov;
      target.expr_region = expr_region;
      target.expr_region_tags = expr_region_tags;
      target.active_region_aliases = active_region_aliases;
      target.dynamic_checks = dynamic_checks;
      target.active_contract_postcondition = active_contract_postcondition;
      target.contract_result_value = contract_result_value;
      target.contract_entry_values = contract_entry_values;
      target.contract_param_entry_values = contract_param_entry_values;
      target.lowering_contract_postcondition = lowering_contract_postcondition;
      target.active_generic_type_subst = active_generic_type_subst;
    }
  };

  InstantiationCtxSnapshot snapshot(ctx);
  // Generic instantiations can be lowered while a caller procedure is still
  // being lowered. Clear caller-local lowering state so the instantiated body
  // does not capture caller scopes/bindings/cleanup state.
  ctx.module_path.clear();
  ctx.scope_stack.clear();
  ctx.binding_states.clear();
  ctx.values.derived_values.clear();
  ctx.temp_sink = nullptr;
  ctx.temp_depth = 0;
  ctx.suppress_temp_at_depth.reset();
  ctx.parallel_collect = nullptr;
  ctx.parallel_collect_depth = 0;
  ctx.capture_env.reset();
  ctx.proc_ret_type = nullptr;
  ctx.current_proc_symbol = symbol_override;
  ctx.current_closure_counter = 0;
  ctx.expr_prov.reset();
  ctx.expr_region.reset();
  ctx.expr_region_tags.reset();
  ctx.active_region_aliases.clear();
  ctx.active_contract_postcondition = nullptr;
  ctx.contract_result_value.reset();
  ctx.contract_entry_values.clear();
  ctx.contract_param_entry_values.clear();
  ctx.lowering_contract_postcondition = false;
  ctx.active_generic_type_subst = type_subst;

  std::vector<std::string> inserted_value_type_keys;
  auto* prev_value_type_insert_sink = ctx.values.value_type_insert_sink;
  ctx.values.value_type_insert_sink = &inserted_value_type_keys;

  ProcIR ir = LowerProc(decl, module_path, ctx);
  ctx.values.value_type_insert_sink = prev_value_type_insert_sink;
  snapshot.Restore(ctx);
  if (prev_value_type_insert_sink && !inserted_value_type_keys.empty()) {
    prev_value_type_insert_sink->insert(prev_value_type_insert_sink->end(),
                                        inserted_value_type_keys.begin(),
                                        inserted_value_type_keys.end());
  }

  ir.symbol = symbol_override;
  ir.defining_module_path = module_path;
  if (HasExportAttr(decl.attrs)) {
    ctx.RegisterExportUnwindMode(ir.symbol, ExportUnwindModeFor(decl.attrs));
  }

  const analysis::ScopeContext scope = ScopeForModule(module_path, ctx);

  const auto instantiate = [&](const analysis::TypeRef& type) -> analysis::TypeRef {
    return InstantiateTypeRef(type, type_subst);
  };

  const std::size_t user_param_count = decl.params.size();
  for (std::size_t i = 0; i < user_param_count && i < ir.params.size(); ++i) {
    analysis::TypeRef lowered = ir.params[i].type;
    if (decl.params[i].type && ctx.sigma) {
      if (const auto lowered_opt = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, decl.params[i].type)) {
        lowered = *lowered_opt;
      }
    }
    ir.params[i].type = instantiate(lowered);
  }

  if (decl.return_type_opt && ctx.sigma) {
    if (const auto lowered_ret = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, decl.return_type_opt)) {
      ir.ret = instantiate(*lowered_ret);
    } else {
      ir.ret = instantiate(ir.ret);
    }
  } else {
    ir.ret = instantiate(ir.ret);
  }

  // Monomorphized procedures MUST be fully concrete across body IR type
  // annotations and per-value type metadata.
  InstantiateIRTypes(ir.body, type_subst);
  for (const auto& name : inserted_value_type_keys) {
    auto it = ctx.values.value_types.find(name);
    if (it != ctx.values.value_types.end()) {
      it->second = instantiate(it->second);
    }
  }

  return ir;
}

void AnchorProcRules() {
  SPEC_RULE("Lower-Proc");
}

}  // namespace ultraviolet::codegen
