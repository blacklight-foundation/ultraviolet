// =============================================================================
// MIGRATION: item/procedure_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.3.1: Procedure Declarations
//   Section 5.2.3: Function Types (T-Proc-As-Value)
//   Section 5.8: Contracts
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/process_config.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/dynamic_context.h"
#include "04_analysis/typing/ffi_by_value.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/expr/path.h"
#include "04_analysis/typing/literals.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/resolve/scopes.h"
#include "02_source/ast/ast.h"
#include "../typecheck_diag_lookup.h"

namespace cursive::analysis {

ScopeList BindTypeParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);
ScopeList BindTypeParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt,
    const std::optional<ast::PredicateClause>& predicate_clause_opt);

namespace {

constexpr std::string_view kMissingTargetProfileDiag =
    "Internal-MissingTargetProfile";

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsProcedureDecl() {
  SPEC_DEF("WF-ProcedureDecl", "5.2.14");
  SPEC_DEF("WF-ProcedureDecl-MissingReturnType", "5.2.14");
  SPEC_DEF("WF-ProcBody-ExplicitReturn-Err", "5.2.14");
  SPEC_DEF("WF-ExternProcDecl", "5.2.14");
  SPEC_DEF("Export-Vis-Err", "5.2.14");
  SPEC_DEF("ExportAbi-Unknown-Err", "5.2.14");
  SPEC_DEF("Export-Return-NotZeroable-Err", "5.2.14");
  SPEC_DEF("Export-ByValue-Err", "5.2.14");
  SPEC_DEF("T-Proc-Decl", "5.3.1");
  SPEC_DEF("T-Proc-As-Value", "5.2.3");
  SPEC_DEF("WF-Contract", "5.8");
  SPEC_DEF("Method-Context-Err", "5.3.4");
  SPEC_DEF("ReturnAnnOk", "5.3.1");
  SPEC_DEF("ParamBinds", "5.3.1");
  SPEC_DEF("ProcReturn", "5.3.1");
  SPEC_DEF("MainSigOk", "5.3.1");
  SPEC_DEF("MainGeneric", "5.3.1");
  SPEC_DEF("Contract-Static-Fail", "14.7.1");
}

struct ProcedureTypePerfStats {
  std::uint64_t decl_calls = 0;
  std::uint64_t body_only_calls = 0;
  std::uint64_t contract_checks = 0;
  std::uint64_t body_type_checks = 0;
  std::uint64_t bind_checks = 0;
  std::uint64_t prov_checks = 0;
  std::uint64_t contract_ms = 0;
  std::uint64_t body_type_ms = 0;
  std::uint64_t bind_ms = 0;
  std::uint64_t prov_ms = 0;
};

static ProcedureTypePerfStats& ProcedurePerfStats() {
  static ProcedureTypePerfStats stats;
  return stats;
}

static bool ProcedurePerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

static bool ProcedurePerfActive() {
  static const bool enabled = ProcedurePerfEnabled();
  return enabled;
}

class ScopedPerfTimer {
 public:
  explicit ScopedPerfTimer(std::uint64_t* slot)
      : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~ScopedPerfTimer() {
    if (!slot_) {
      return;
    }
    *slot_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
  }

 private:
  std::uint64_t* slot_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
};

// =============================================================================
// HELPERS
// =============================================================================

// Check if parameter names are distinct
static bool DistinctParamNames(const std::vector<ast::Param>& params) {
  if (params.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& param : params) {
    if (!names.insert(param.name).second) {
      return false;
    }
  }
  return true;
}

static bool HasReservedSelfParam(const std::vector<ast::Param>& params) {
  for (const auto& param : params) {
    if (IdEq(param.name, "self")) {
      return true;
    }
  }
  return false;
}

// Check if return type annotation is present
static bool ReturnAnnOk(const std::shared_ptr<ast::Type>& ret_opt) {
  return ret_opt != nullptr;
}

// Check if the procedure is the main entry point
static bool IsMainProcedure(std::string_view name) {
  return name == "main";
}

// Validate main procedure signature
static bool MainSigOk(const ScopeContext& ctx, const ast::ProcedureDecl& decl) {
  // Must be public
  if (decl.vis != ast::Visibility::Public) {
    return false;
  }
  // Must have exactly one parameter
  if (decl.params.size() != 1) {
    return false;
  }
  const auto& param = decl.params[0];
  // Parameter must be move mode (or unspecified which defaults to move for Context)
  if (param.mode.has_value() && *param.mode != ast::ParamMode::Move) {
    return false;
  }
  // Parameter type must be a Context bundle.
  if (!param.type || !IsContextBundleType(ctx, *param.type)) {
    return false;
  }
  // Return type must be i32
  const auto* ret = decl.return_type_opt
                        ? std::get_if<ast::TypePrim>(&decl.return_type_opt->node)
                        : nullptr;
  return ret && ret->name == "i32";
}

// Check if main procedure has generics (not allowed)
static bool MainGeneric(const ast::ProcedureDecl& decl) {
  return !ast::TypeParamsOpt(decl.generic_params).empty();
}

static bool IsBareTestContextType(const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return false;
  }
  const auto* path = std::get_if<ast::TypePathType>(&type->node);
  return path && path->generic_args.empty() &&
         path->path == ast::TypePath{"TestContext"};
}

static std::optional<std::string_view> ValidateTestProcedureShape(
    const ast::ProcedureDecl& decl) {
  if (!HasAttribute(decl.attrs, attrs::kTest)) {
    return std::nullopt;
  }

  if (!decl.body || !ast::TypeParamsOpt(decl.generic_params).empty() ||
      !decl.visibility_explicit || !ReturnAnnOk(decl.return_type_opt) ||
      decl.params.size() > 1) {
    return "E-TST-0104";
  }

  if (!decl.contract.has_value() || !decl.contract->postcondition) {
    return "E-TST-0106";
  }

  if (decl.params.size() == 1 && !IsBareTestContextType(decl.params[0].type)) {
    return "E-TST-0105";
  }

  return std::nullopt;
}

static core::Span SpanOfStmt(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) -> core::Span { return node.span; },
      stmt);
}

static std::optional<core::Span> ProcedureBodyFailureSpan(
    const std::shared_ptr<ast::Block>& body) {
  if (!body) {
    return std::nullopt;
  }
  if (!body->stmts.empty()) {
    return SpanOfStmt(body->stmts.front());
  }
  if (body->tail_opt) {
    return body->tail_opt->span;
  }
  return body->span;
}

// Lower type with well-formedness check
static LowerTypeResult LowerTypeWithWF(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    return {false, wf.diag_id, {}};
  }
  return lowered;
}

static void EmitTypecheckDiag(core::DiagnosticStream& diags,
                              std::string_view diag_id,
                              const std::optional<core::Span>& span,
                              const std::string& detail = {}) {
  EmitResolvedTypecheckDiagnostic(diags, diag_id, span, detail);
}

static void EmitSupplementalBorrowDiag(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Block>& body,
    core::DiagnosticStream& diags,
    const std::optional<std::string_view>& primary_diag_id) {
  if (!body) {
    return;
  }
  auto primary_is_call_move_missing = [&]() {
    if (primary_diag_id.has_value() && *primary_diag_id == "E-SEM-2534") {
      return true;
    }
    if (!diags.empty() && diags.back().code == "E-SEM-2534") {
      return true;
    }
    return false;
  };
  const auto bind_result = BindCheckBody(ctx, module_path, params, body, std::nullopt);
  if (!bind_result.ok && bind_result.diag_id.has_value()) {
    if (!primary_diag_id.has_value() || *primary_diag_id != *bind_result.diag_id) {
      EmitTypecheckDiag(diags, *bind_result.diag_id, bind_result.span);
    }
    return;
  }
  if (primary_is_call_move_missing()) {
    EmitTypecheckDiag(diags, "E-MOD-2411", std::nullopt);
  }
}

static void EmitBorrowMoveMissingFromRecentDiags(
    core::DiagnosticStream& diags,
    std::size_t begin_index) {
  bool saw_call_move_missing = false;
  bool saw_borrow_move_missing = false;
  for (std::size_t i = begin_index; i < diags.size(); ++i) {
    if (diags[i].code == "E-SEM-2534") {
      saw_call_move_missing = true;
    } else if (diags[i].code == "E-MOD-2411") {
      saw_borrow_move_missing = true;
    }
  }
  if (saw_call_move_missing && !saw_borrow_move_missing) {
    EmitTypecheckDiag(diags, "E-MOD-2411", std::nullopt);
  }
}

// Lower return type (defaulting to unit)
static LowerTypeResult LowerReturnType(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type_opt) {
  if (!type_opt) {
    return {true, std::nullopt, MakeTypePrim("()")};
  }
  return LowerTypeWithWF(ctx, type_opt);
}

// Check if a block has an explicit return statement
static bool HasExplicitReturn(const ast::Block& block) {
  auto stmtHasExplicitReturn = [&](const auto& self, const ast::Stmt& stmt) -> bool {
    return std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return true;
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            return node.body && HasExplicitReturn(*node.body);
          }
          return false;
        },
        stmt);
  };
  if (block.tail_opt) {
    return false;
  }
  return !block.stmts.empty() && stmtHasExplicitReturn(stmtHasExplicitReturn, block.stmts.back());
}

static bool ExprMayNeedDynamicRuntime(const ast::ExprPtr& expr);
static bool BlockMayNeedDynamicRuntime(const ast::Block& block);

static bool InvariantNeedsRuntimeCheck(const ast::LoopInvariant& invariant) {
  if (!invariant.predicate) {
    return false;
  }
  StaticProofContext proof_ctx;
  const auto proof = StaticProof(proof_ctx, invariant.predicate);
  return !proof.provable;
}

static bool ArgListMayNeedDynamicRuntime(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (ExprMayNeedDynamicRuntime(arg.value)) {
      return true;
    }
  }
  return false;
}

static bool FieldInitsMayNeedDynamicRuntime(
    const std::vector<ast::FieldInit>& fields) {
  for (const auto& field : fields) {
    if (ExprMayNeedDynamicRuntime(field.value)) {
      return true;
    }
  }
  return false;
}

static bool ExprMayNeedDynamicRuntime(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::ErrorExpr> ||
                      std::is_same_v<T, ast::LiteralExpr> ||
                      std::is_same_v<T, ast::IdentifierExpr> ||
                      std::is_same_v<T, ast::QualifiedNameExpr> ||
                      std::is_same_v<T, ast::PathExpr> ||
                      std::is_same_v<T, ast::PtrNullExpr> ||
                      std::is_same_v<T, ast::SizeofExpr> ||
                      std::is_same_v<T, ast::AlignofExpr> ||
                      std::is_same_v<T, ast::ResultExpr> ||
                      std::is_same_v<T, ast::FenceExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return std::visit(
              [&](const auto& args) -> bool {
                using A = std::decay_t<decltype(args)>;
                if constexpr (std::is_same_v<A, ast::ParenArgs>) {
                  return ArgListMayNeedDynamicRuntime(args.args);
                } else {
                  return FieldInitsMayNeedDynamicRuntime(args.fields);
                }
              },
              node.args);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          return std::visit(
              [&](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& elem : payload.elements) {
                    if (ExprMayNeedDynamicRuntime(elem)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  return FieldInitsMayNeedDynamicRuntime(payload.fields);
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprMayNeedDynamicRuntime(node.lhs) ||
                 ExprMayNeedDynamicRuntime(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprMayNeedDynamicRuntime(node.lhs) ||
                 ExprMayNeedDynamicRuntime(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::EntryExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr> ||
                             std::is_same_v<T, ast::WaitExpr> ||
                             std::is_same_v<T, ast::AttributedExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr>) {
          if constexpr (std::is_same_v<T, ast::CastExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            return ExprMayNeedDynamicRuntime(node.place);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            return ExprMayNeedDynamicRuntime(node.place);
          } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            return ExprMayNeedDynamicRuntime(node.expr);
          } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            return ExprMayNeedDynamicRuntime(node.handle);
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            return ExprMayNeedDynamicRuntime(node.expr);
          } else {
            return ExprMayNeedDynamicRuntime(node.value);
          }
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprMayNeedDynamicRuntime(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool may_need_dynamic_runtime = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (may_need_dynamic_runtime) {
              return;
            }
            if (ExprMayNeedDynamicRuntime(elem)) {
              may_need_dynamic_runtime = true;
            }
          });
          return may_need_dynamic_runtime;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprMayNeedDynamicRuntime(node.value) ||
                 ExprMayNeedDynamicRuntime(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          return FieldInitsMayNeedDynamicRuntime(node.fields);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprMayNeedDynamicRuntime(node.cond) ||
                 ExprMayNeedDynamicRuntime(node.then_expr) ||
                 ExprMayNeedDynamicRuntime(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprMayNeedDynamicRuntime(node.scrutinee)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (ExprMayNeedDynamicRuntime(arm.body)) {
              return true;
            }
          }
          return ExprMayNeedDynamicRuntime(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprMayNeedDynamicRuntime(node.scrutinee) ||
                 ExprMayNeedDynamicRuntime(node.then_expr) ||
                 ExprMayNeedDynamicRuntime(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value() &&
              InvariantNeedsRuntimeCheck(*node.invariant_opt)) {
            return true;
          }
          return node.body && BlockMayNeedDynamicRuntime(*node.body);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          if (node.invariant_opt.has_value() &&
              InvariantNeedsRuntimeCheck(*node.invariant_opt)) {
            return true;
          }
          return ExprMayNeedDynamicRuntime(node.cond) ||
                 (node.body && BlockMayNeedDynamicRuntime(*node.body));
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          if (node.invariant_opt.has_value() &&
              InvariantNeedsRuntimeCheck(*node.invariant_opt)) {
            return true;
          }
          return ExprMayNeedDynamicRuntime(node.iter) ||
                 (node.body && BlockMayNeedDynamicRuntime(*node.body));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block && BlockMayNeedDynamicRuntime(*node.block);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return ExprMayNeedDynamicRuntime(node.body);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ExprMayNeedDynamicRuntime(node.lhs) ||
                 ExprMayNeedDynamicRuntime(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr> ||
                             std::is_same_v<T, ast::PropagateExpr>) {
          if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            return ExprMayNeedDynamicRuntime(node.base);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            return ExprMayNeedDynamicRuntime(node.base);
          } else {
            return ExprMayNeedDynamicRuntime(node.value);
          }
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          // Dynamic scopes can require runtime index checks.
          return true;
        } else if constexpr (std::is_same_v<T, ast::CallExpr> ||
                             std::is_same_v<T, ast::MethodCallExpr>) {
          // Dynamic scopes can trigger runtime contract checks at call sites.
          return true;
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            if (ExprMayNeedDynamicRuntime(arm.expr) ||
                ExprMayNeedDynamicRuntime(arm.handler.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& inner : node.exprs) {
            if (ExprMayNeedDynamicRuntime(inner)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (ExprMayNeedDynamicRuntime(node.domain)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprMayNeedDynamicRuntime(opt.value)) {
              return true;
            }
          }
          return node.body && BlockMayNeedDynamicRuntime(*node.body);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            if (ExprMayNeedDynamicRuntime(opt.value)) {
              return true;
            }
          }
          return node.body && BlockMayNeedDynamicRuntime(*node.body);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (ExprMayNeedDynamicRuntime(node.range)) {
            return true;
          }
          if (node.key_clause.has_value()) {
            for (const auto& seg : node.key_clause->key_path.segs) {
              if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
                if (ExprMayNeedDynamicRuntime(idx->expr)) {
                  return true;
                }
              }
            }
          }
          for (const auto& opt : node.opts) {
            if (ExprMayNeedDynamicRuntime(opt.chunk_expr)) {
              return true;
            }
            if (ExprMayNeedDynamicRuntime(opt.workgroup_expr)) {
              return true;
            }
          }
          return node.body && BlockMayNeedDynamicRuntime(*node.body);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool BlockMayNeedDynamicRuntime(const ast::Block& block) {
  for (const auto& stmt : block.stmts) {
    const bool stmt_needs_runtime = std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::LetStmt>) {
            return ExprMayNeedDynamicRuntime(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
            return ExprMayNeedDynamicRuntime(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            (void)node;
            return false;
          } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            return ExprMayNeedDynamicRuntime(node.place) ||
                   ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            return ExprMayNeedDynamicRuntime(node.place) ||
                   ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            return ExprMayNeedDynamicRuntime(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
            return node.body && BlockMayNeedDynamicRuntime(*node.body);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            return ExprMayNeedDynamicRuntime(node.opts_opt) ||
                   (node.body && BlockMayNeedDynamicRuntime(*node.body));
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            return node.body && BlockMayNeedDynamicRuntime(*node.body);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return ExprMayNeedDynamicRuntime(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
            return ExprMayNeedDynamicRuntime(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
            return node.body && BlockMayNeedDynamicRuntime(*node.body);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            // Key blocks can lower to runtime synchronization in dynamic scope.
            return true;
          } else {
            return false;
          }
        },
        stmt);
    if (stmt_needs_runtime) {
      return true;
    }
  }
  return ExprMayNeedDynamicRuntime(block.tail_opt);
}

static bool ContractMayNeedDynamicRuntime(const ast::ContractClause& contract) {
  auto predicate_needs_runtime = [](const ast::ExprPtr& predicate) {
    if (!predicate) {
      return false;
    }
    StaticProofContext proof_ctx;
    const auto proof = StaticProof(proof_ctx, predicate);
    return !proof.provable;
  };

  return predicate_needs_runtime(contract.precondition) ||
         predicate_needs_runtime(contract.postcondition);
}

static void EmitDynamicNoRuntimeWarningIfNeeded(
    const ast::ProcedureDecl& decl,
    core::DiagnosticStream& diags) {
  if (!IsDynamicDecl(decl)) {
    return;
  }

  bool runtime_effect_possible = false;
  if (decl.contract.has_value() &&
      ContractMayNeedDynamicRuntime(*decl.contract)) {
    runtime_effect_possible = true;
  }
  if (!runtime_effect_possible && decl.body &&
      BlockMayNeedDynamicRuntime(*decl.body)) {
    runtime_effect_possible = true;
  }
  if (runtime_effect_possible) {
    return;
  }

  if (auto diag = core::MakeDiagnosticById("W-CON-0401", decl.span)) {
    core::Emit(diags, *diag);
  }
}

static std::string NormalizeAttrLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

struct UnwindAttrCheck {
  bool has_attr = false;
  bool duplicate = false;
  bool invalid = false;
  std::string mode;
};

static UnwindAttrCheck CheckUnwindAttr(const ast::AttributeList& attr_list) {
  UnwindAttrCheck check;
  std::vector<const ast::AttributeItem*> unwind_attrs;
  for (const auto& attr : attr_list) {
    if (IdEq(std::string_view(attr.name), ::cursive::analysis::attrs::kUnwind)) {
      unwind_attrs.push_back(&attr);
    }
  }

  if (unwind_attrs.empty()) {
    return check;
  }

  check.has_attr = true;
  if (unwind_attrs.size() > 1) {
    check.duplicate = true;
    return check;
  }

  const ast::AttributeItem& attr = *unwind_attrs.front();
  if (attr.args.size() != 1 || attr.args.front().key.has_value()) {
    check.invalid = true;
    return check;
  }

  const auto* token = std::get_if<ast::Token>(&attr.args.front().value);
  if (!token || token->kind != lexer::TokenKind::StringLiteral) {
    check.invalid = true;
    return check;
  }

  const std::string mode = NormalizeAttrLiteral(token->lexeme);
  if (mode != "abort" && mode != "catch") {
    check.invalid = true;
    return check;
  }

  check.mode = mode;
  return check;
}

static bool IsCatchUnwind(const ast::AttributeList& attr_list) {
  const auto unwind_check = CheckUnwindAttr(attr_list);
  if (!unwind_check.has_attr || unwind_check.duplicate || unwind_check.invalid) {
    return false;
  }
  return unwind_check.mode == "catch";
}

static bool ContractClauseHasDirectDynamicAttr(
    const std::optional<ast::ContractClause>& contract) {
  if (!contract.has_value()) {
    return false;
  }
  auto has_dynamic = [](const ast::ExprPtr& expr) {
    if (!expr) {
      return false;
    }
    const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node);
    if (!attributed) {
      return false;
    }
    return HasAttribute(attributed->attrs, attrs::kDynamic);
  };
  return has_dynamic(contract->precondition) || has_dynamic(contract->postcondition);
}

static bool IsValidFfiAbi(std::string_view abi) {
  return abi == "C" ||
         abi == "C-unwind" ||
         abi == "system" ||
         abi == "stdcall" ||
         abi == "fastcall" ||
         abi == "vectorcall";
}

static bool IsSupportedFfiAbiForProfile(std::string_view abi,
                                        project::TargetProfile profile) {
  if (abi == "stdcall" || abi == "fastcall" || abi == "vectorcall") {
    return profile == project::TargetProfile::X86_64Win64;
  }
  return true;
}

static std::optional<std::string> ExportAbiValue(
    const ast::AttributeList& attrs_list) {
  const auto abi = GetAttributeValue(attrs_list, attrs::kExport);
  if (!abi.has_value()) {
    return std::nullopt;
  }
  return NormalizeAttrLiteral(*abi);
}

static std::optional<std::string> HostExportAbiValue(
    const ast::AttributeList& attrs_list) {
  const auto abi = GetAttributeValue(attrs_list, attrs::kHostExport);
  if (!abi.has_value()) {
    return std::nullopt;
  }
  return NormalizeAttrLiteral(*abi);
}

static bool ExportAbiIsC(const ast::AttributeList& attrs_list) {
  const auto abi = ExportAbiValue(attrs_list);
  return abi.has_value() && *abi == "C";
}

struct MangleAttrCheck {
  bool has_attr = false;
  bool invalid = false;
  bool conflicting = false;
  bool none_mode = false;
  std::string explicit_name;
};

static MangleAttrCheck CheckMangleAttr(const ast::AttributeList& attrs_list) {
  MangleAttrCheck check;
  bool has_none_mode = false;
  std::optional<std::string> symbol_mode_value;

  for (const auto& attr : attrs_list) {
    if (!IdEq(std::string_view(attr.name), ::cursive::analysis::attrs::kMangle)) {
      continue;
    }

    check.has_attr = true;
    if (attr.args.size() != 1) {
      check.invalid = true;
      return check;
    }

    const auto& arg = attr.args.front();
    if (arg.key.has_value() && *arg.key != "mode") {
      check.invalid = true;
      return check;
    }

    const auto* token = std::get_if<ast::Token>(&arg.value);
    if (!token) {
      check.invalid = true;
      return check;
    }
    const std::string raw = NormalizeAttrLiteral(token->lexeme);
    if (raw.empty()) {
      check.invalid = true;
      return check;
    }
    if (raw == "none" && token->kind != lexer::TokenKind::StringLiteral) {
      if (symbol_mode_value.has_value()) {
        check.conflicting = true;
        return check;
      }
      has_none_mode = true;
      continue;
    }
    if (token->kind == lexer::TokenKind::StringLiteral) {
      if (has_none_mode) {
        check.conflicting = true;
        return check;
      }
      if (symbol_mode_value.has_value() && *symbol_mode_value != raw) {
        check.conflicting = true;
        return check;
      }
      symbol_mode_value = raw;
      continue;
    }
    check.invalid = true;
    return check;
  }

  if (has_none_mode) {
    check.none_mode = true;
    return check;
  }
  if (symbol_mode_value.has_value()) {
    check.explicit_name = *symbol_mode_value;
  }
  return check;
}

static bool HasInlineAlways(const ast::AttributeList& attrs_list) {
  for (const auto& attr : attrs_list) {
    if (attr.name != attrs::kInline) {
      continue;
    }
    if (attr.args.empty()) {
      return false;
    }
    for (const auto& arg : attr.args) {
      if (arg.key.has_value()) {
        continue;
      }
      const auto* token = std::get_if<ast::Token>(&arg.value);
      if (!token) {
        continue;
      }
      if (NormalizeAttrLiteral(token->lexeme) == "always") {
        return true;
      }
    }
  }
  return false;
}

static const project::Assembly* CurrentAssembly(const ScopeContext& ctx) {
  const auto* project = Project(ctx);
  if (!project) {
    return nullptr;
  }
  if (!CurrentModule(ctx).empty()) {
    for (const auto& assembly : project->assemblies) {
      if (IdEq(assembly.name, CurrentModule(ctx).front())) {
        return &assembly;
      }
    }
  }
  return &project->assembly;
}

static std::string_view CurrentAssemblyName(const ScopeContext& ctx) {
  if (const auto* assembly = CurrentAssembly(ctx)) {
    return assembly->name;
  }
  if (!CurrentModule(ctx).empty()) {
    return CurrentModule(ctx).front();
  }
  static const std::string empty;
  return empty;
}

static bool ProcedureHasAttr(const ast::ProcedureDecl& proc,
                             std::string_view attr_name) {
  return HasAttribute(proc.attrs, attr_name);
}

static bool AssemblyContainsProcedureAttr(const ScopeContext& ctx,
                                          std::string_view assembly_name,
                                          std::string_view attr_name) {
  for (const auto& module : ctx.sigma.mods) {
    if (module.path.empty() || !IdEq(module.path.front(), assembly_name)) {
      continue;
    }
    for (const auto& item : module.items) {
      const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
      if (proc && ProcedureHasAttr(*proc, attr_name)) {
        return true;
      }
    }
  }
  return false;
}

static bool AssemblyHasMixedForeignExportModes(const ScopeContext& ctx) {
  const std::string_view assembly_name = CurrentAssemblyName(ctx);
  if (assembly_name.empty()) {
    return false;
  }
  return AssemblyContainsProcedureAttr(ctx, assembly_name, attrs::kExport) &&
         AssemblyContainsProcedureAttr(ctx, assembly_name, attrs::kHostExport);
}

static bool ExprContainsDirectCall(const ast::ExprPtr& expr,
                                   std::string_view proc_name);

static bool BlockContainsDirectCall(const std::shared_ptr<ast::Block>& block,
                                    std::string_view proc_name);
static bool ExprContainsValueReference(
    const ast::ExprPtr& expr,
    const ast::ModulePath& current_module,
    const ast::ModulePath& target_module,
    std::string_view proc_name,
    bool in_callee_position = false);
static bool BlockContainsValueReference(
    const std::shared_ptr<ast::Block>& block,
    const ast::ModulePath& current_module,
    const ast::ModulePath& target_module,
    std::string_view proc_name);
static bool StmtContainsValueReference(const ast::Stmt& stmt,
                                       const ast::ModulePath& current_module,
                                       const ast::ModulePath& target_module,
                                       std::string_view proc_name);

static bool StmtContainsDirectCall(const ast::Stmt& stmt,
                                   std::string_view proc_name) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return ExprContainsDirectCall(node.binding.init, proc_name);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ExprContainsDirectCall(node.place, proc_name) ||
                 ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprContainsDirectCall(node.value_opt, proc_name);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return BlockContainsDirectCall(node.body, proc_name);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprContainsDirectCall(node.opts_opt, proc_name) ||
                 BlockContainsDirectCall(node.body, proc_name);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          return BlockContainsDirectCall(node.body, proc_name);
        } else {
          return false;
        }
      },
      stmt);
}

static bool BlockContainsDirectCall(const std::shared_ptr<ast::Block>& block,
                                    std::string_view proc_name) {
  if (!block) {
    return false;
  }
  for (const auto& stmt : block->stmts) {
    if (StmtContainsDirectCall(stmt, proc_name)) {
      return true;
    }
  }
  return ExprContainsDirectCall(block->tail_opt, proc_name);
}

static bool ExprContainsDirectCall(const ast::ExprPtr& expr,
                                   std::string_view proc_name) {
  if (!expr) {
    return false;
  }

  auto is_direct_callee = [&](const ast::ExprPtr& callee) -> bool {
    if (!callee) {
      return false;
    }
    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
      return IdEq(ident->name, proc_name);
    }
    if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
      return path->path.empty() && IdEq(path->name, proc_name);
    }
    return false;
  };

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (is_direct_callee(node.callee)) {
            return true;
          }
          if (ExprContainsDirectCall(node.callee, proc_name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsDirectCall(arg.value, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (IdEq(node.name, proc_name)) {
            return true;
          }
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            for (const auto& arg : std::get<ast::ParenArgs>(node.args).args) {
              if (ExprContainsDirectCall(arg.value, proc_name)) {
                return true;
              }
            }
            return false;
          }
          for (const auto& field : std::get<ast::BraceArgs>(node.args).fields) {
            if (ExprContainsDirectCall(field.value, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsDirectCall(node.receiver, proc_name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsDirectCall(arg.value, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsDirectCall(node.lhs, proc_name) ||
                 ExprContainsDirectCall(node.rhs, proc_name);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsDirectCall(node.base, proc_name);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsDirectCall(node.base, proc_name);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsDirectCall(node.base, proc_name) ||
                 ExprContainsDirectCall(node.index, proc_name);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprContainsDirectCall(node.lhs, proc_name) ||
                 ExprContainsDirectCall(node.rhs, proc_name);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsDirectCall(node.place, proc_name);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsDirectCall(node.place, proc_name);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsDirectCall(elem, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_direct_call = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_direct_call) {
              return;
            }
            if (ExprContainsDirectCall(elem, proc_name)) {
              contains_direct_call = true;
            }
          });
          return contains_direct_call;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsDirectCall(node.value, proc_name) ||
                 ExprContainsDirectCall(node.count, proc_name);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsDirectCall(field.value, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
            for (const auto& elem :
                 std::get<ast::EnumPayloadParen>(*node.payload_opt).elements) {
              if (ExprContainsDirectCall(elem, proc_name)) {
                return true;
              }
            }
            return false;
          }
          for (const auto& field :
               std::get<ast::EnumPayloadBrace>(*node.payload_opt).fields) {
            if (ExprContainsDirectCall(field.value, proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsDirectCall(node.cond, proc_name) ||
                 ExprContainsDirectCall(node.then_expr, proc_name) ||
                 ExprContainsDirectCall(node.else_expr, proc_name);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsDirectCall(node.scrutinee, proc_name)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (ExprContainsDirectCall(arm.body, proc_name)) {
              return true;
            }
          }
          return ExprContainsDirectCall(node.else_expr, proc_name);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsDirectCall(node.scrutinee, proc_name) ||
                 ExprContainsDirectCall(node.then_expr, proc_name) ||
                 ExprContainsDirectCall(node.else_expr, proc_name);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return BlockContainsDirectCall(node.block, proc_name);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return BlockContainsDirectCall(node.block, proc_name);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsDirectCall(node.value, proc_name);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsDirectCall(node.expr, proc_name);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool ModulePathEqLocal(const ast::ModulePath& lhs,
                              const ast::ModulePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

static bool ExprContainsValueReference(
    const ast::ExprPtr& expr,
    const ast::ModulePath& current_module,
    const ast::ModulePath& target_module,
    std::string_view proc_name,
    bool in_callee_position) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return !in_callee_position &&
                 ModulePathEqLocal(current_module, target_module) &&
                 IdEq(node.name, proc_name);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (in_callee_position || !IdEq(node.name, proc_name)) {
            return false;
          }
          if (node.path.empty()) {
            return ModulePathEqLocal(current_module, target_module);
          }
          return ModulePathEqLocal(node.path, target_module);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return !in_callee_position && IdEq(node.name, proc_name) &&
                 ModulePathEqLocal(node.path, target_module);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprContainsValueReference(node.callee, current_module, target_module,
                                         proc_name, true)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsValueReference(arg.value, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            for (const auto& arg : std::get<ast::ParenArgs>(node.args).args) {
              if (ExprContainsValueReference(arg.value, current_module, target_module,
                                             proc_name)) {
                return true;
              }
            }
            return false;
          }
          for (const auto& field : std::get<ast::BraceArgs>(node.args).fields) {
            if (ExprContainsValueReference(field.value, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsValueReference(node.receiver, current_module, target_module,
                                         proc_name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsValueReference(arg.value, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsValueReference(node.lhs, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.rhs, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsValueReference(node.base, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsValueReference(node.base, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsValueReference(node.base, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.index, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprContainsValueReference(node.lhs, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.rhs, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsValueReference(node.place, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsValueReference(node.place, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsValueReference(elem, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_value_reference = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_value_reference) {
              return;
            }
            if (ExprContainsValueReference(elem, current_module, target_module,
                                           proc_name)) {
              contains_value_reference = true;
            }
          });
          return contains_value_reference;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.count, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsValueReference(field.value, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
            for (const auto& elem :
                 std::get<ast::EnumPayloadParen>(*node.payload_opt).elements) {
              if (ExprContainsValueReference(elem, current_module, target_module,
                                             proc_name)) {
                return true;
              }
            }
            return false;
          }
          for (const auto& field :
               std::get<ast::EnumPayloadBrace>(*node.payload_opt).fields) {
            if (ExprContainsValueReference(field.value, current_module, target_module,
                                           proc_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsValueReference(node.cond, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.then_expr, current_module,
                                            target_module, proc_name) ||
                 ExprContainsValueReference(node.else_expr, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsValueReference(node.scrutinee, current_module, target_module,
                                         proc_name)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ExprContainsValueReference(case_clause.body, current_module,
                                           target_module,
                                           proc_name)) {
              return true;
            }
          }
          return ExprContainsValueReference(node.else_expr, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsValueReference(node.scrutinee, current_module,
                                            target_module, proc_name) ||
                 ExprContainsValueReference(node.then_expr, current_module,
                                            target_module, proc_name) ||
                 ExprContainsValueReference(node.else_expr, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return BlockContainsValueReference(node.block, current_module, target_module,
                                             proc_name);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return BlockContainsValueReference(node.block, current_module, target_module,
                                             proc_name);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsValueReference(node.expr, current_module, target_module,
                                            proc_name);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool StmtContainsValueReference(const ast::Stmt& stmt,
                                       const ast::ModulePath& current_module,
                                       const ast::ModulePath& target_module,
                                       std::string_view proc_name) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return ExprContainsValueReference(node.binding.init, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ExprContainsValueReference(node.place, current_module, target_module,
                                            proc_name) ||
                 ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprContainsValueReference(node.value, current_module, target_module,
                                            proc_name);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprContainsValueReference(node.value_opt, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return BlockContainsValueReference(node.body, current_module, target_module,
                                             proc_name);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprContainsValueReference(node.opts_opt, current_module,
                                            target_module, proc_name) ||
                 BlockContainsValueReference(node.body, current_module,
                                             target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          return BlockContainsValueReference(node.body, current_module, target_module,
                                             proc_name);
        } else {
          return false;
        }
      },
      stmt);
}

static bool BlockContainsValueReference(
    const std::shared_ptr<ast::Block>& block,
    const ast::ModulePath& current_module,
    const ast::ModulePath& target_module,
    std::string_view proc_name) {
  if (!block) {
    return false;
  }
  for (const auto& stmt : block->stmts) {
    if (StmtContainsValueReference(stmt, current_module, target_module,
                                   proc_name)) {
      return true;
    }
  }
  return ExprContainsValueReference(block->tail_opt, current_module, target_module,
                                    proc_name);
}

static bool ItemContainsValueReference(const ast::ASTItem& item,
                                       const ast::ModulePath& current_module,
                                       const ast::ModulePath& target_module,
                                       std::string_view proc_name) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          return BlockContainsValueReference(node.body, current_module, target_module,
                                             proc_name);
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          return ExprContainsValueReference(node.binding.init, current_module,
                                            target_module, proc_name);
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          for (const auto& member : node.members) {
            if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
              if (ExprContainsValueReference(field->init_opt, current_module,
                                             target_module, proc_name)) {
                return true;
              }
              continue;
            }
            if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
              if (BlockContainsValueReference(method->body, current_module,
                                              target_module, proc_name)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          for (const auto& class_item : node.items) {
            if (const auto* method =
                    std::get_if<ast::ClassMethodDecl>(&class_item)) {
              if (BlockContainsValueReference(method->body_opt, current_module,
                                              target_module, proc_name)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              if (const auto* method =
                      std::get_if<ast::StateMethodDecl>(&member)) {
                if (BlockContainsValueReference(method->body, current_module,
                                                target_module, proc_name)) {
                  return true;
                }
                continue;
              }
              if (const auto* transition =
                      std::get_if<ast::TransitionDecl>(&member)) {
                if (BlockContainsValueReference(transition->body, current_module,
                                                target_module, proc_name)) {
                  return true;
                }
              }
            }
          }
          return false;
        } else {
          return false;
        }
      },
      item);
}

static bool HasProcedureAddressTaken(const ScopeContext& ctx,
                                     const ast::ModulePath& target_module,
                                     std::string_view proc_name) {
  const Sigma* sigma_ptr =
      ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  for (const auto& module : sigma_ptr->mods) {
    for (const auto& item : module.items) {
      if (ItemContainsValueReference(item, module.path, target_module,
                                     proc_name)) {
        return true;
      }
    }
  }
  return false;
}

static std::optional<std::string_view> ProcedureWarningInlineAlways(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl,
    const ast::ModulePath& module_path) {
  if (!HasInlineAlways(decl.attrs)) {
    return std::nullopt;
  }
  const bool recursive =
      decl.body && BlockContainsDirectCall(decl.body, decl.name);
  const bool address_taken = HasProcedureAddressTaken(ctx, module_path, decl.name);
  if (!recursive && !address_taken) {
    return std::nullopt;
  }
  return "W-MOD-2452";
}

static void EmitProcedureFfiWarnings(const ast::ProcedureDecl& decl,
                                     core::DiagnosticStream& diags) {
  const bool has_export = HasAttribute(decl.attrs, attrs::kExport);
  const bool has_host_export = HasAttribute(decl.attrs, attrs::kHostExport);
  const bool has_foreign_export = has_export || has_host_export;
  const auto mangle_check = CheckMangleAttr(decl.attrs);
  if (has_export && mangle_check.has_attr && !mangle_check.invalid &&
      mangle_check.none_mode && ExportAbiIsC(decl.attrs)) {
    EmitTypecheckDiag(diags, "W-SYS-3350", std::optional<core::Span>(decl.span));
  }
  const auto unwind_check = CheckUnwindAttr(decl.attrs);
  if (has_foreign_export && unwind_check.has_attr && !unwind_check.duplicate &&
      !unwind_check.invalid && unwind_check.mode == "abort") {
    EmitTypecheckDiag(diags, "W-SYS-3355", std::optional<core::Span>(decl.span));
  }
}

static std::optional<std::string_view> ValidateProcedureFfiAttributes(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl) {
  const bool has_export = HasAttribute(decl.attrs, attrs::kExport);
  const bool has_host_export = HasAttribute(decl.attrs, attrs::kHostExport);
  const bool has_foreign_export = has_export || has_host_export;
  const auto mangle_check = CheckMangleAttr(decl.attrs);
  const auto unwind_check = CheckUnwindAttr(decl.attrs);
  const bool has_unwind = unwind_check.has_attr;
  const auto export_abi = ExportAbiValue(decl.attrs);
  const auto host_export_abi = HostExportAbiValue(decl.attrs);
  const auto& foreign_abi = has_host_export ? host_export_abi : export_abi;

  if (has_foreign_export && decl.vis != ast::Visibility::Public) {
    SPEC_RULE("Export-Vis-Err");
    return "E-SYS-3353";
  }

  if (has_foreign_export) {
    const auto profile = RequireSelectedTargetProfile(ctx);
    if (!profile.has_value()) {
      return kMissingTargetProfileDiag;
    }
    if (!foreign_abi.has_value() || foreign_abi->empty() ||
        !IsValidFfiAbi(*foreign_abi) ||
        !IsSupportedFfiAbiForProfile(*foreign_abi, *profile)) {
      SPEC_RULE("ExportAbi-Unknown-Err");
      return "E-SYS-3352";
    }
  }

  if (mangle_check.has_attr) {
    if (mangle_check.conflicting) {
      return "E-SYS-3351";
    }
    if (mangle_check.invalid) {
      return "E-SYS-3341";
    }
    if (!has_foreign_export) {
      return mangle_check.none_mode ? "E-SYS-3350" : "E-SYS-3340";
    }
    if (!mangle_check.none_mode && mangle_check.explicit_name.empty()) {
      return "E-SYS-3341";
    }
  }

  if (has_unwind) {
    if (!has_foreign_export) {
      return "E-SYS-3356";
    }
    if (unwind_check.duplicate) {
      SPEC_RULE("UnwindMode-Duplicate-Err");
      return "E-FFI-0350";
    }
    if (unwind_check.invalid) {
      SPEC_RULE("UnwindMode-Invalid-Err");
      return "E-SYS-3355";
    }
    if (unwind_check.mode == "catch" &&
        (!foreign_abi.has_value() || *foreign_abi != "C-unwind")) {
      SPEC_RULE("UnwindMode-Invalid-Err");
      return "E-SYS-3355";
    }
  }

  if (has_foreign_export && AssemblyHasMixedForeignExportModes(ctx)) {
    return "E-SYS-3358";
  }

  if (has_host_export) {
    const auto* assembly = CurrentAssembly(ctx);
    if (!assembly || assembly->kind != "library") {
      return "E-SYS-3357";
    }
    if (!ast::TypeParamsOpt(decl.generic_params).empty()) {
      return "E-TYP-2634";
    }
    if (decl.params.empty() || !decl.params.front().type ||
        !IsContextBundleType(ctx, *decl.params.front().type)) {
      return "E-TYP-2632";
    }
    if (!IsHostedContextBundleType(ctx, *decl.params.front().type)) {
      return "E-TYP-2636";
    }
    if (decl.params.front().mode.has_value()) {
      return "E-TYP-2633";
    }
  }

  return std::nullopt;
}

static std::optional<std::string_view> ValidateProcedureVerificationModeContext(
    const ast::ProcedureDecl& decl) {
  if (HasAttribute(decl.attrs, attrs::kStatic)) {
    return "E-MOD-2452";
  }
  return std::nullopt;
}

}  // namespace

void LogProcedureTypePerfSummary() {
  if (!ProcedurePerfEnabled()) {
    return;
  }
  const auto& stats = ProcedurePerfStats();
  if (stats.decl_calls == 0 && stats.body_only_calls == 0) {
    return;
  }
  std::fprintf(stderr,
               "[cursive] sema perf=procedure decl_calls=%llu "
               "body_only_calls=%llu contract_checks=%llu "
               "body_type_checks=%llu bind_checks=%llu prov_checks=%llu "
               "contract_ms=%llu body_type_ms=%llu bind_ms=%llu "
               "prov_ms=%llu\n",
               static_cast<unsigned long long>(stats.decl_calls),
               static_cast<unsigned long long>(stats.body_only_calls),
               static_cast<unsigned long long>(stats.contract_checks),
               static_cast<unsigned long long>(stats.body_type_checks),
               static_cast<unsigned long long>(stats.bind_checks),
               static_cast<unsigned long long>(stats.prov_checks),
               static_cast<unsigned long long>(stats.contract_ms),
               static_cast<unsigned long long>(stats.body_type_ms),
               static_cast<unsigned long long>(stats.bind_ms),
               static_cast<unsigned long long>(stats.prov_ms));
  std::fflush(stderr);
}

// =============================================================================
// EXPORTED: TypeProcedureDecl
// =============================================================================

ProcedureDeclResult TypeProcedureDecl(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsProcedureDecl();
  auto& perf_stats = ProcedurePerfStats();
  const bool perf_on = ProcedurePerfActive();
  if (perf_on) {
    ++perf_stats.decl_calls;
    static bool perf_banner_printed = false;
    if (!perf_banner_printed) {
      perf_banner_printed = true;
      std::cerr << "[cursive] sema perf=procedure begin\n";
    }
  }
  ProcedureDeclResult result;
  result.ok = true;

  // Validate declaration attributes before typing the signature/body.
  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Procedure);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  if (const auto test_diag = ValidateTestProcedureShape(decl)) {
    result.ok = false;
    result.diag_id = *test_diag;
    return result;
  }

  // Check return type annotation is present
  if (!ReturnAnnOk(decl.return_type_opt)) {
    SPEC_RULE("WF-ProcedureDecl-MissingReturnType");
    SPEC_RULE("ReturnAnnOk-Err");
    result.ok = false;
    result.diag_id = "WF-ProcedureDecl-MissingReturnType";
    return result;
  }

  // Check parameter names are distinct
  if (!DistinctParamNames(decl.params)) {
    SPEC_RULE("ParamBinds-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-SEM-2713";
    return result;
  }

  if (HasReservedSelfParam(decl.params)) {
    SPEC_RULE("Method-Context-Err");
    result.ok = false;
    result.diag_id = "E-SEM-3011";
    return result;
  }

  if (const auto ffi_diag = ValidateProcedureFfiAttributes(ctx, decl)) {
    result.ok = false;
    result.diag_id = *ffi_diag;
    return result;
  }
  if (const auto verification_mode_diag =
          ValidateProcedureVerificationModeContext(decl)) {
    result.ok = false;
    result.diag_id = *verification_mode_diag;
    return result;
  }
  if (const auto inline_warn =
          ProcedureWarningInlineAlways(ctx, decl, module_path)) {
    EmitTypecheckDiag(diags, *inline_warn, std::optional<core::Span>(decl.span));
  }
  EmitProcedureFfiWarnings(decl, diags);

  // Check main procedure constraints
  if (IsMainProcedure(decl.name)) {
    if (MainGeneric(decl)) {
      SPEC_RULE("MainGeneric-Err");
      result.ok = false;
      result.diag_id = "E-MOD-2432";
      return result;
    }
    if (!MainSigOk(ctx, decl)) {
      SPEC_RULE("MainSigOk-Err");
      result.ok = false;
      result.diag_id = "E-MOD-2431";
      return result;
    }
  }

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  ScopeContext proc_ctx = ctx;
  proc_ctx.sigma_source = ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  proc_ctx.scopes =
      BindTypeParams(ctx, decl.generic_params, decl.predicate_clause_opt);

  // Process where clauses
  std::vector<std::string> type_param_names;
  for (const auto& gp : gen_params.params) {
    type_param_names.push_back(gp.name);
  }
  if (decl.predicate_clause_opt.has_value()) {
    const auto where_result = ProcessWhereClause(
        proc_ctx, *decl.predicate_clause_opt, type_param_names);
    if (!where_result.ok) {
      result.ok = false;
      result.diag_id = where_result.diag_id;
      return result;
    }
  }

  // Build procedure signature
  const auto sig =
      BuildProcedureSignature(proc_ctx, decl.params, decl.return_type_opt);
  if (!sig.ok) {
    result.ok = false;
    result.diag_id = sig.diag_id;
    return result;
  }

  result.func_type = sig.func_type;

  const bool has_export = HasAttribute(decl.attrs, attrs::kExport);
  const bool has_host_export = HasAttribute(decl.attrs, attrs::kHostExport);
  if (has_export || has_host_export) {
    if (!FfiSafeType(proc_ctx, sig.return_type)) {
      SPEC_RULE("FfiSafe-Return-Err");
      result.ok = false;
          result.diag_id =
              FfiSafeDiagForType(proc_ctx, module_path, sig.return_type)
                  .value_or("E-TYP-2623");
      return result;
    }
    if (!InferCapabilitiesFromType(proc_ctx, module_path, sig.return_type)
             .IsEmpty()) {
      SPEC_RULE("FfiSafe-Prohibited-Err");
      result.ok = false;
      result.diag_id = "E-TYP-2623";
      return result;
    }

    const std::size_t visible_param_begin = has_host_export ? 1 : 0;
    if (const auto* fn = std::get_if<TypeFunc>(&sig.func_type->node)) {
      for (std::size_t i = visible_param_begin; i < fn->params.size(); ++i) {
        const auto& param = fn->params[i];
        if (!FfiSafeType(proc_ctx, param.type)) {
          SPEC_RULE("FfiSafe-Param-Err");
          result.ok = false;
          result.diag_id =
              FfiSafeDiagForType(proc_ctx, module_path, param.type)
                  .value_or("E-TYP-2623");
          return result;
        }
        if (!InferCapabilitiesFromType(proc_ctx, module_path, param.type)
                 .IsEmpty()) {
          SPEC_RULE("FfiSafe-Prohibited-Err");
          result.ok = false;
          result.diag_id = "E-TYP-2623";
          return result;
        }
      }
    } else {
      result.ok = false;
      result.diag_id = "E-TYP-2623";
      return result;
    }

    bool by_value_ok = FfiByValueOk(proc_ctx, sig.return_type);
    if (const auto* fn = std::get_if<TypeFunc>(&sig.func_type->node)) {
      for (std::size_t i = visible_param_begin; i < fn->params.size(); ++i) {
        const auto& param = fn->params[i];
        if (!FfiByValueOk(proc_ctx, param.type)) {
          by_value_ok = false;
          break;
        }
      }
    } else {
      by_value_ok = false;
    }

    if (!by_value_ok) {
      SPEC_RULE("Export-ByValue-Err");
      result.ok = false;
      result.diag_id = "E-TYP-2630";
      return result;
    }

    if (IsCatchUnwind(decl.attrs) && !ZeroableType(proc_ctx, sig.return_type)) {
      SPEC_RULE("Export-Return-NotZeroable-Err");
      result.ok = false;
      result.diag_id =
          has_host_export ? "E-TYP-2635" : "Export-Return-NotZeroable-Err";
      return result;
    }
  }

  // Build type environment with parameter bindings
  TypeEnv env;
  env.scopes.emplace_back();
  for (const auto& binding : sig.bindings) {
    TypeBinding param_binding;
    param_binding.mut = ast::Mutability::Let;
    param_binding.type = binding.second;
    param_binding.storage_type = binding.second;
    param_binding.provenance_kind = BindingProvenanceSeedKind::Param;
    env.scopes.back()[IdKeyOf(binding.first)] = std::move(param_binding);
  }

  // Check contract clause if present
  if (decl.contract.has_value()) {
    if (perf_on) {
      ++perf_stats.contract_checks;
    }
    ScopedPerfTimer contract_timer(perf_on ? &perf_stats.contract_ms : nullptr);
    if (ContractClauseHasDirectDynamicAttr(decl.contract)) {
      result.ok = false;
      result.diag_id = "E-CON-0410";
      return result;
    }

    // Build contract context
    ContractContext contract_ctx;
    contract_ctx.scope_ctx = &proc_ctx;
    for (const auto& binding : sig.bindings) {
      contract_ctx.params[binding.first] = binding.second;
    }
    for (const auto& param : decl.params) {
      if (param.mode.has_value() &&
          *param.mode == ast::ParamMode::Move) {
        contract_ctx.moved_params.insert(param.name);
      }
    }
    contract_ctx.return_type = sig.return_type;

    // Validate contract intrinsics
    const auto intrinsics_check = ValidateContractIntrinsics(*decl.contract, contract_ctx);
    if (!intrinsics_check.ok) {
      result.ok = false;
      result.diag_id = intrinsics_check.diag_id;
      return result;
    }

    // Check contract well-formedness
    const auto contract_check = CheckContractWellFormed(contract_ctx, *decl.contract);
    if (!contract_check.ok) {
      result.ok = false;
      result.diag_id = contract_check.diag_id;
      return result;
    }

  }

  // Type the body if present
  if (decl.body) {
    // For non-unit return types, check for explicit return
    const bool is_unit = TypeEquiv(sig.return_type, MakeTypePrim("()")).equiv;
    if (!is_unit && !HasExplicitReturn(*decl.body)) {
      SPEC_RULE("WF-ProcBody-ExplicitReturn-Err");
      SPEC_RULE("ProcReturn-Missing-Err");
      result.ok = false;
      result.diag_id = "E-TYP-1507";
      return result;
    }

    StmtTypeContext type_ctx;
    type_ctx.return_type = sig.return_type;
    type_ctx.ffi_export_boundary =
        HasAttribute(decl.attrs, attrs::kExport) ||
        HasAttribute(decl.attrs, attrs::kHostExport);
    proc_ctx.diagnostics = &diags;
    type_ctx.diags = &diags;
    type_ctx.env_ref = &env;
    const std::array<DynamicScopeAncestor, 1> ancestors{
        MakeDynamicScopeAncestor(decl.attrs, decl.span)};
    type_ctx.contract_dynamic =
        ComputeDynamicContext(decl.body->span, ancestors);
    type_ctx.test_postcondition_runtime = HasAttribute(decl.attrs, attrs::kTest);
    OpaqueReturnState opaque_return_state;
    if (type_ctx.return_type &&
        std::holds_alternative<TypeOpaque>(type_ctx.return_type->node)) {
      const auto& opaque =
          std::get<TypeOpaque>(type_ctx.return_type->node);
      opaque_return_state.origin = opaque.origin;
      opaque_return_state.class_path = opaque.class_path;
      opaque_return_state.underlying = {};
      type_ctx.opaque_return = &opaque_return_state;
    }
    if (decl.contract.has_value()) {
      type_ctx.contract = &*decl.contract;
    }

    ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
      return TypeExpr(proc_ctx, type_ctx, inner, env);
    };
    IdentTypeFn type_ident = [&](std::string_view name) -> ExprTypeResult {
      return expr::TypeIdentifierExprImpl(
          proc_ctx, ast::IdentifierExpr{std::string(name)}, env);
    };
    PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
      return TypePlace(proc_ctx, type_ctx, inner, env);
    };

    if (decl.contract.has_value()) {
      auto check_contract_predicate =
          [&](const ast::ExprPtr& predicate,
              ContractPhase phase) -> std::optional<std::string_view> {
        if (!predicate) {
          return std::nullopt;
        }

        StmtTypeContext contract_type_ctx = type_ctx;
        contract_type_ctx.contract_phase = phase;
        contract_type_ctx.require_pure = true;
        const auto typed = TypeExpr(proc_ctx, contract_type_ctx, predicate, env);
        if (!typed.ok) {
          return typed.diag_id.value_or("WF-Contract");
        }
        if (!TypeEquiv(typed.type, MakeTypePrim("bool")).equiv) {
          return std::string_view("WF-Contract");
        }
        return std::nullopt;
      };

      if (const auto diag = check_contract_predicate(
              decl.contract->precondition, ContractPhase::Precondition)) {
        result.ok = false;
        result.diag_id = *diag;
        return result;
      }
      if (const auto diag = check_contract_predicate(
              decl.contract->postcondition, ContractPhase::Postcondition)) {
        result.ok = false;
        result.diag_id = *diag;
        return result;
      }
    }

    const auto diag_count_before_body = diags.size();
    if (perf_on) {
      ++perf_stats.body_type_checks;
    }
    const auto body_result = [&]() {
      ScopedPerfTimer body_timer(perf_on ? &perf_stats.body_type_ms : nullptr);
      return TypeBlock(proc_ctx, type_ctx, *decl.body, env, type_expr, type_ident,
                       type_place, &env);
    }();
    EmitBorrowMoveMissingFromRecentDiags(diags, diag_count_before_body);
    if (!body_result.ok) {
      const auto diag_id = body_result.diag_id.has_value()
                               ? body_result.diag_id
                               : std::optional<std::string_view>{"E-TYP-1530"};
      if (core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline")) {
        std::fprintf(stderr,
                     "[proc-body-fail] %s diag=%s detail=%s\n",
                     decl.name.c_str(),
                     diag_id.has_value() ? std::string(*diag_id).c_str() : "<none>",
                     body_result.diag_detail.empty() ? "<none>"
                                                     : body_result.diag_detail.c_str());
      }
      EmitSupplementalBorrowDiag(proc_ctx, module_path, decl.params, decl.body,
                                 diags, diag_id);
      result.ok = false;
      result.diag_id = diag_id;
      if (!body_result.diag_detail.empty()) {
        result.diag_detail = body_result.diag_detail;
      } else {
        result.diag_detail =
            "procedure body typing failed without statement-level diagnostic";
      }
      result.diag_span = body_result.diag_span.has_value()
                             ? body_result.diag_span
                             : ProcedureBodyFailureSpan(decl.body);
      return result;
    }

    if (type_ctx.opaque_return && type_ctx.opaque_return->origin &&
        type_ctx.opaque_return->underlying) {
      auto& sigma_mut = const_cast<ScopeContext&>(ctx).sigma;
      sigma_mut.opaque_underlying_by_class_path[
          PathKeyOf(type_ctx.opaque_return->class_path)] =
          type_ctx.opaque_return->underlying;
    }

    // Check body type matches return type.
    // Opaque returns are validated at return sites (T-Opaque-Return).
    if (body_result.type && type_ctx.opaque_return == nullptr) {
      const auto sub = Subtyping(proc_ctx, body_result.type, sig.return_type);
      if (!sub.ok) {
        result.ok = false;
        result.diag_id = sub.diag_id;
        return result;
      }
      if (!sub.subtype) {
        SPEC_RULE("Return-Type-Err");
        result.ok = false;
        result.diag_id = "E-SEM-3161";
        return result;
      }
    }

    // Borrow checking
    if (perf_on) {
      ++perf_stats.bind_checks;
    }
    const auto bind_result = [&]() {
      ScopedPerfTimer bind_timer(perf_on ? &perf_stats.bind_ms : nullptr);
      return BindCheckBody(proc_ctx, module_path, decl.params, decl.body,
                           std::nullopt);
    }();
    if (!bind_result.ok) {
      result.ok = false;
      result.diag_id = bind_result.diag_id;
      return result;
    }

    // Provenance/region escape checking
    if (perf_on) {
      ++perf_stats.prov_checks;
    }
    const auto prov_result = [&]() {
      ScopedPerfTimer prov_timer(perf_on ? &perf_stats.prov_ms : nullptr);
      return ProvBindCheck(proc_ctx, module_path, decl.params, decl.body,
                           std::nullopt, &diags);
    }();
    if (!prov_result.ok) {
      result.ok = false;
      result.diag_id = prov_result.diag_id;
      return result;
    }
  }

  EmitDynamicNoRuntimeWarningIfNeeded(decl, diags);
  SPEC_RULE("WF-ProcedureDecl");
  SPEC_RULE("T-Proc-Decl-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeProcedureDeclSignature (first pass - signature only)
// =============================================================================

ProcedureDeclResult TypeProcedureDeclSignature(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl) {
  SpecDefsProcedureDecl();
  ProcedureDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Procedure);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  if (const auto test_diag = ValidateTestProcedureShape(decl)) {
    result.ok = false;
    result.diag_id = *test_diag;
    return result;
  }

  // Check return type annotation
  if (!ReturnAnnOk(decl.return_type_opt)) {
    SPEC_RULE("WF-ProcedureDecl-MissingReturnType");
    SPEC_RULE("ReturnAnnOk-Err");
    result.ok = false;
    result.diag_id = "WF-ProcedureDecl-MissingReturnType";
    return result;
  }

  // Check parameter names distinct
  if (!DistinctParamNames(decl.params)) {
    SPEC_RULE("ParamBinds-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-SEM-2713";
    return result;
  }

  if (HasReservedSelfParam(decl.params)) {
    SPEC_RULE("Method-Context-Err");
    result.ok = false;
    result.diag_id = "E-SEM-3011";
    return result;
  }

  if (const auto ffi_diag = ValidateProcedureFfiAttributes(ctx, decl)) {
    result.ok = false;
    result.diag_id = *ffi_diag;
    return result;
  }
  if (const auto verification_mode_diag =
          ValidateProcedureVerificationModeContext(decl)) {
    result.ok = false;
    result.diag_id = *verification_mode_diag;
    return result;
  }

  if (ContractClauseHasDirectDynamicAttr(decl.contract)) {
    result.ok = false;
    result.diag_id = "E-CON-0410";
    return result;
  }

  // Process generic parameters
  const auto gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  ScopeContext proc_ctx = ctx;
  proc_ctx.sigma_source = ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  proc_ctx.scopes =
      BindTypeParams(ctx, decl.generic_params, decl.predicate_clause_opt);

  // Build signature
  const auto sig =
      BuildProcedureSignature(proc_ctx, decl.params, decl.return_type_opt);
  if (!sig.ok) {
    result.ok = false;
    result.diag_id = sig.diag_id;
    return result;
  }

  result.func_type = sig.func_type;

  SPEC_RULE("T-Proc-Sig-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeProcedureDeclBody (second pass - body only)
// =============================================================================

ProcedureDeclResult TypeProcedureDeclBody(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl,
    const ast::ModulePath& module_path,
    const TypeRef& return_type,
    core::DiagnosticStream& diags) {
  SpecDefsProcedureDecl();
  auto& perf_stats = ProcedurePerfStats();
  const bool perf_on = ProcedurePerfActive();
  if (perf_on) {
    ++perf_stats.body_only_calls;
  }
  ProcedureDeclResult result;
  result.ok = true;

  if (!decl.body) {
    return result;
  }

  ScopeContext proc_ctx = ctx;
  proc_ctx.sigma_source = ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  proc_ctx.scopes =
      BindTypeParams(ctx, decl.generic_params, decl.predicate_clause_opt);

  // Rebuild parameter environment
  TypeEnv env;
  env.scopes.emplace_back();
  for (const auto& param : decl.params) {
    const auto lowered = LowerTypeWithWF(proc_ctx, param.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }
    TypeBinding binding;
    binding.mut = ast::Mutability::Let;
    binding.type = lowered.type;
    binding.storage_type = lowered.type;
    binding.provenance_kind = BindingProvenanceSeedKind::Param;
    env.scopes.back()[IdKeyOf(param.name)] = std::move(binding);
  }

  // Check for explicit return on non-unit types
  const bool is_unit = TypeEquiv(return_type, MakeTypePrim("()")).equiv;
  if (!is_unit && !HasExplicitReturn(*decl.body)) {
    SPEC_RULE("WF-ProcBody-ExplicitReturn-Err");
    SPEC_RULE("ProcReturn-Missing-Err");
    result.ok = false;
    result.diag_id = "E-TYP-1507";
    return result;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = return_type;
  type_ctx.ffi_export_boundary =
      HasAttribute(decl.attrs, attrs::kExport) ||
      HasAttribute(decl.attrs, attrs::kHostExport);
  proc_ctx.diagnostics = &diags;
  type_ctx.diags = &diags;
  type_ctx.env_ref = &env;
  const std::array<DynamicScopeAncestor, 1> ancestors{
      MakeDynamicScopeAncestor(decl.attrs, decl.span)};
  type_ctx.contract_dynamic =
      ComputeDynamicContext(decl.body->span, ancestors);
  type_ctx.test_postcondition_runtime = HasAttribute(decl.attrs, attrs::kTest);
  OpaqueReturnState opaque_return_state;
  if (type_ctx.return_type &&
      std::holds_alternative<TypeOpaque>(type_ctx.return_type->node)) {
    const auto& opaque = std::get<TypeOpaque>(type_ctx.return_type->node);
    opaque_return_state.origin = opaque.origin;
    opaque_return_state.class_path = opaque.class_path;
    opaque_return_state.underlying = {};
    type_ctx.opaque_return = &opaque_return_state;
  }
  if (decl.contract.has_value()) {
    type_ctx.contract = &*decl.contract;
  }

  ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(proc_ctx, type_ctx, inner, env);
  };
  IdentTypeFn type_ident = [&](std::string_view name) -> ExprTypeResult {
    return expr::TypeIdentifierExprImpl(
        proc_ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(proc_ctx, type_ctx, inner, env);
  };

  const auto diag_count_before_body = diags.size();
  if (perf_on) {
    ++perf_stats.body_type_checks;
  }
  const auto body_result = [&]() {
    ScopedPerfTimer body_timer(perf_on ? &perf_stats.body_type_ms : nullptr);
    return TypeBlock(proc_ctx, type_ctx, *decl.body, env, type_expr, type_ident,
                     type_place, &env);
  }();
  EmitBorrowMoveMissingFromRecentDiags(diags, diag_count_before_body);
  if (!body_result.ok) {
    const auto diag_id = body_result.diag_id.has_value()
                             ? body_result.diag_id
                             : std::optional<std::string_view>{"E-TYP-1530"};
    if (core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline")) {
      std::fprintf(stderr,
                   "[proc-body-fail] %s diag=%s detail=%s\n",
                   decl.name.c_str(),
                   diag_id.has_value() ? std::string(*diag_id).c_str() : "<none>",
                   body_result.diag_detail.empty() ? "<none>"
                                                   : body_result.diag_detail.c_str());
    }
    EmitSupplementalBorrowDiag(proc_ctx, module_path, decl.params, decl.body,
                               diags, diag_id);
    result.ok = false;
    result.diag_id = diag_id;
    if (!body_result.diag_detail.empty()) {
      result.diag_detail = body_result.diag_detail;
    } else {
      result.diag_detail =
          "procedure body typing failed without statement-level diagnostic";
    }
    result.diag_span = body_result.diag_span.has_value()
                           ? body_result.diag_span
                           : ProcedureBodyFailureSpan(decl.body);
    return result;
  }

  if (type_ctx.opaque_return && type_ctx.opaque_return->origin &&
      type_ctx.opaque_return->underlying) {
    auto& sigma_mut = const_cast<ScopeContext&>(ctx).sigma;
    sigma_mut.opaque_underlying_by_class_path[
        PathKeyOf(type_ctx.opaque_return->class_path)] =
        type_ctx.opaque_return->underlying;
  }

  // Check body type.
  // Opaque returns are validated at return sites (T-Opaque-Return).
  if (body_result.type && type_ctx.opaque_return == nullptr) {
    const auto sub = Subtyping(proc_ctx, body_result.type, return_type);
    if (!sub.ok || !sub.subtype) {
      SPEC_RULE("Return-Type-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3161";
      return result;
    }
  }

  // Borrow check
  if (perf_on) {
    ++perf_stats.bind_checks;
  }
  const auto bind_result = [&]() {
    ScopedPerfTimer bind_timer(perf_on ? &perf_stats.bind_ms : nullptr);
    return BindCheckBody(proc_ctx, module_path, decl.params, decl.body,
                         std::nullopt);
  }();
  if (!bind_result.ok) {
    result.ok = false;
    result.diag_id = bind_result.diag_id;
    return result;
  }

  // Provenance/region escape checking
  if (perf_on) {
    ++perf_stats.prov_checks;
  }
  const auto prov_result = [&]() {
    ScopedPerfTimer prov_timer(perf_on ? &perf_stats.prov_ms : nullptr);
    return ProvBindCheck(proc_ctx, module_path, decl.params, decl.body,
                         std::nullopt, &diags);
  }();
  if (!prov_result.ok) {
    result.ok = false;
    result.diag_id = prov_result.diag_id;
    return result;
  }

  EmitDynamicNoRuntimeWarningIfNeeded(decl, diags);
  SPEC_RULE("T-Proc-Body-Ok");
  return result;
}

}  // namespace cursive::analysis
