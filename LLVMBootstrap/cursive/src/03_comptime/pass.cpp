#include "03_comptime/comptime.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "02_source/module_paths.h"
#include "03_comptime/comptime_internal.h"

namespace cursive::frontend {
namespace {

void LogComptimeProgress(const std::string& message) {
  if (!core::IsDebugEnabled("pipeline") && !core::IsDebugEnabled("phases")) {
    return;
  }
  std::cerr << "[cursive] comptime " << message << "\n";
  std::cerr.flush();
}

bool HasFilesAttr(const ast::AttributeList& attrs) {
  return ast::has_attribute(attrs, "files");
}

bool HasFilesAttr(const ast::AttrOpt& attrs_opt) {
  return attrs_opt.has_value() && HasFilesAttr(*attrs_opt);
}

bool HasEmitAttr(const ast::AttributeList& attrs) {
  return ast::has_attribute(attrs, "emit");
}

bool HasDeriveAttr(const ast::AttributeList& attrs) {
  return ast::has_attribute(attrs, "derive");
}

bool QuoteMentionsFiles(const ast::QuoteExpr& quote) {
  for (const auto& token : quote.tokens) {
    if (token.lexeme == "files") {
      return true;
    }
  }
  return false;
}

bool TypeMayNeedProjectFiles(const ast::TypePtr& type);
bool ExprMayNeedProjectFiles(const ast::ExprPtr& expr);
bool PatternMayNeedProjectFiles(const ast::PatternPtr& pattern);
bool BlockMayNeedProjectFiles(const ast::BlockPtr& block);
bool StmtMayNeedProjectFiles(const ast::Stmt& stmt);
bool ItemMayNeedProjectFiles(const ast::ASTItem& item);

bool ArgsMayNeedProjectFiles(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (ExprMayNeedProjectFiles(arg.value)) {
      return true;
    }
  }
  return false;
}

bool FieldInitsMayNeedProjectFiles(const std::vector<ast::FieldInit>& fields) {
  for (const auto& field : fields) {
    if (ExprMayNeedProjectFiles(field.value)) {
      return true;
    }
  }
  return false;
}

bool ApplyArgsMayNeedProjectFiles(const ast::ApplyArgs& args) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          return ArgsMayNeedProjectFiles(node.args);
        } else {
          return FieldInitsMayNeedProjectFiles(node.fields);
        }
      },
      args);
}

bool KeyPathMayNeedProjectFiles(const ast::KeyPathExpr& path) {
  for (const auto& seg : path.segs) {
    if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      if (ExprMayNeedProjectFiles(index->expr)) {
        return true;
      }
    }
  }
  return false;
}

bool LoopInvariantMayNeedProjectFiles(
    const std::optional<ast::LoopInvariant>& invariant_opt) {
  return invariant_opt.has_value() &&
         ExprMayNeedProjectFiles(invariant_opt->predicate);
}

bool ContractMayNeedProjectFiles(
    const std::optional<ast::ContractClause>& contract_opt) {
  if (!contract_opt.has_value()) {
    return false;
  }
  return ExprMayNeedProjectFiles(contract_opt->precondition) ||
         ExprMayNeedProjectFiles(contract_opt->postcondition);
}

bool WhereClauseMayNeedProjectFiles(
    const std::optional<ast::PredicateClause>& where_opt) {
  if (!where_opt.has_value()) {
    return false;
  }
  for (const auto& pred : *where_opt) {
    if (TypeMayNeedProjectFiles(pred.type)) {
      return true;
    }
  }
  return false;
}

bool GenericParamsMayNeedProjectFiles(
    const std::optional<ast::GenericParams>& params_opt) {
  if (!params_opt.has_value()) {
    return false;
  }
  for (const auto& param : params_opt->params) {
    if (TypeMayNeedProjectFiles(param.default_type)) {
      return true;
    }
  }
  return false;
}

bool ParamsMayNeedProjectFiles(const std::vector<ast::Param>& params) {
  for (const auto& param : params) {
    if (TypeMayNeedProjectFiles(param.type)) {
      return true;
    }
  }
  return false;
}

bool ReceiverMayNeedProjectFiles(const ast::Receiver& receiver) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ReceiverExplicit>) {
          return TypeMayNeedProjectFiles(node.type);
        } else {
          return false;
        }
      },
      receiver);
}

bool TypeModalRefMayNeedProjectFiles(const ast::TypeModalRef& modal_ref) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          for (const auto& arg : node.generic_args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else {
          for (const auto& arg : node.args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        }
      },
      modal_ref);
}

bool TypeMayNeedProjectFiles(const ast::TypePtr& type) {
  if (!type) {
    return false;
  }
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return TypeMayNeedProjectFiles(node.base);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& member : node.types) {
            if (TypeMayNeedProjectFiles(member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            if (TypeMayNeedProjectFiles(param.type)) {
              return true;
            }
          }
          return TypeMayNeedProjectFiles(node.ret);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            if (TypeMayNeedProjectFiles(param.type)) {
              return true;
            }
          }
          if (TypeMayNeedProjectFiles(node.ret)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (TypeMayNeedProjectFiles(dep.type)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (TypeMayNeedProjectFiles(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return TypeMayNeedProjectFiles(node.element) ||
                 ExprMayNeedProjectFiles(node.length);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice> ||
                             std::is_same_v<T, ast::TypeSafePtr> ||
                             std::is_same_v<T, ast::TypeRawPtr>) {
          return TypeMayNeedProjectFiles(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return TypeMayNeedProjectFiles(node.base);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          for (const auto& arg : node.generic_args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          for (const auto& arg : node.args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          if (TypeModalRefMayNeedProjectFiles(node.modal_ref)) {
            return true;
          }
          for (const auto& arg : node.generic_args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return TypeMayNeedProjectFiles(node.base) ||
                 ExprMayNeedProjectFiles(node.predicate);
        } else if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return ExprMayNeedProjectFiles(node.expr);
        } else {
          return false;
        }
      },
      type->node);
}

bool PatternMayNeedProjectFiles(const ast::PatternPtr& pattern) {
  if (!pattern) {
    return false;
  }
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          return TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (PatternMayNeedProjectFiles(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            if (PatternMayNeedProjectFiles(field.pattern_opt)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          return std::visit(
              [](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    if (PatternMayNeedProjectFiles(elem)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  for (const auto& field : payload.fields) {
                    if (PatternMayNeedProjectFiles(field.pattern_opt)) {
                      return true;
                    }
                  }
                  return false;
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return false;
          }
          for (const auto& field : node.fields_opt->fields) {
            if (PatternMayNeedProjectFiles(field.pattern_opt)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return PatternMayNeedProjectFiles(node.lo) ||
                 PatternMayNeedProjectFiles(node.hi);
        } else if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return ExprMayNeedProjectFiles(node.expr);
        } else {
          return false;
        }
      },
      pattern->node);
}

bool EnumPayloadMayNeedProjectFiles(const ast::EnumPayload& payload) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
          for (const auto& elem : node.elements) {
            if (ExprMayNeedProjectFiles(elem)) {
              return true;
            }
          }
          return false;
        } else {
          return FieldInitsMayNeedProjectFiles(node.fields);
        }
      },
      payload);
}

bool ExprMayNeedProjectFiles(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (HasFilesAttr(ast::ExprAttrList(*expr))) {
    return true;
  }
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return ApplyArgsMayNeedProjectFiles(node.args);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          return node.payload_opt.has_value() &&
                 EnumPayloadMayNeedProjectFiles(*node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          return TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::QuoteExpr>) {
          return QuoteMentionsFiles(node);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprMayNeedProjectFiles(node.lhs) ||
                 ExprMayNeedProjectFiles(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprMayNeedProjectFiles(node.lhs) ||
                 ExprMayNeedProjectFiles(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprMayNeedProjectFiles(node.value) ||
                 TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return ExprMayNeedProjectFiles(node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return ExprMayNeedProjectFiles(node.place);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprMayNeedProjectFiles(node.expr);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return ExprMayNeedProjectFiles(node.handle);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprMayNeedProjectFiles(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& inner) {
            found = found || ExprMayNeedProjectFiles(inner);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprMayNeedProjectFiles(node.value) ||
                 ExprMayNeedProjectFiles(node.count);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr> ||
                             std::is_same_v<T, ast::AlignofExpr>) {
          return TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          return FieldInitsMayNeedProjectFiles(node.fields);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprMayNeedProjectFiles(node.cond) ||
                 ExprMayNeedProjectFiles(node.then_expr) ||
                 ExprMayNeedProjectFiles(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprMayNeedProjectFiles(node.scrutinee) ||
                 PatternMayNeedProjectFiles(node.pattern) ||
                 ExprMayNeedProjectFiles(node.then_expr) ||
                 ExprMayNeedProjectFiles(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprMayNeedProjectFiles(node.scrutinee) ||
              ExprMayNeedProjectFiles(node.else_expr)) {
            return true;
          }
          for (const auto& clause : node.cases) {
            if (PatternMayNeedProjectFiles(clause.pattern) ||
                ExprMayNeedProjectFiles(clause.body)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return LoopInvariantMayNeedProjectFiles(node.invariant_opt) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return ExprMayNeedProjectFiles(node.cond) ||
                 LoopInvariantMayNeedProjectFiles(node.invariant_opt) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return PatternMayNeedProjectFiles(node.pattern) ||
                 TypeMayNeedProjectFiles(node.type_opt) ||
                 ExprMayNeedProjectFiles(node.iter) ||
                 LoopInvariantMayNeedProjectFiles(node.invariant_opt) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return BlockMayNeedProjectFiles(node.block);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          const auto& attrs = ast::AttrListOf(node.attrs_opt);
          return HasFilesAttr(attrs) || HasEmitAttr(attrs) ||
                 ExprMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          return ExprMayNeedProjectFiles(node.cond) ||
                 BlockMayNeedProjectFiles(node.then_block) ||
                 BlockMayNeedProjectFiles(node.else_block_opt);
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          return PatternMayNeedProjectFiles(node.pattern) ||
                 TypeMayNeedProjectFiles(node.type_opt) ||
                 ExprMayNeedProjectFiles(node.iter) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return HasFilesAttr(node.attrs) || ExprMayNeedProjectFiles(node.expr);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return TypeMayNeedProjectFiles(node.from) ||
                 TypeMayNeedProjectFiles(node.to) ||
                 ExprMayNeedProjectFiles(node.value);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (const auto& param : node.params) {
            if (TypeMayNeedProjectFiles(param.type_opt)) {
              return true;
            }
          }
          return TypeMayNeedProjectFiles(node.ret_type_opt) ||
                 ExprMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ExprMayNeedProjectFiles(node.lhs) ||
                 ExprMayNeedProjectFiles(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprMayNeedProjectFiles(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprMayNeedProjectFiles(node.base) ||
                 ExprMayNeedProjectFiles(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprMayNeedProjectFiles(node.callee) ||
              ArgsMayNeedProjectFiles(node.args)) {
            return true;
          }
          for (const auto& arg : node.generic_args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          if (ExprMayNeedProjectFiles(node.callee) ||
              ArgsMayNeedProjectFiles(node.args)) {
            return true;
          }
          for (const auto& arg : node.type_args) {
            if (TypeMayNeedProjectFiles(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return ExprMayNeedProjectFiles(node.receiver) ||
                 ArgsMayNeedProjectFiles(node.args);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            if (ExprMayNeedProjectFiles(arm.expr) ||
                PatternMayNeedProjectFiles(arm.pattern) ||
                ExprMayNeedProjectFiles(arm.handler.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& inner : node.exprs) {
            if (ExprMayNeedProjectFiles(inner)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (ExprMayNeedProjectFiles(node.domain) ||
              BlockMayNeedProjectFiles(node.body)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprMayNeedProjectFiles(opt.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          if (BlockMayNeedProjectFiles(node.body)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprMayNeedProjectFiles(opt.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (PatternMayNeedProjectFiles(node.pattern) ||
              ExprMayNeedProjectFiles(node.range) ||
              BlockMayNeedProjectFiles(node.body)) {
            return true;
          }
          if (node.key_clause.has_value() &&
              KeyPathMayNeedProjectFiles(node.key_clause->key_path)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprMayNeedProjectFiles(opt.chunk_expr) ||
                ExprMayNeedProjectFiles(opt.workgroup_expr)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return ExprMayNeedProjectFiles(node.expr);
        } else if constexpr (std::is_same_v<T, ast::SpliceIdentNode>) {
          return ExprMayNeedProjectFiles(node.name_expr);
        } else {
          return false;
        }
      },
      expr->node);
}

bool BindingMayNeedProjectFiles(const ast::Binding& binding) {
  return HasFilesAttr(binding.attrs) ||
         PatternMayNeedProjectFiles(binding.pat) ||
         TypeMayNeedProjectFiles(binding.type_opt) ||
         ExprMayNeedProjectFiles(binding.init);
}

bool BlockMayNeedProjectFiles(const ast::BlockPtr& block) {
  if (!block) {
    return false;
  }
  for (const auto& stmt : block->stmts) {
    if (StmtMayNeedProjectFiles(stmt)) {
      return true;
    }
  }
  return ExprMayNeedProjectFiles(block->tail_opt);
}

bool StmtMayNeedProjectFiles(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return BindingMayNeedProjectFiles(node.binding);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          return ExprMayNeedProjectFiles(node.place) ||
                 ExprMayNeedProjectFiles(node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ExprMayNeedProjectFiles(node.place) ||
                 ExprMayNeedProjectFiles(node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprMayNeedProjectFiles(node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt>) {
          return BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprMayNeedProjectFiles(node.opts_opt) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprMayNeedProjectFiles(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::CtStmt>) {
          return HasFilesAttr(node.attrs) || HasEmitAttr(node.attrs) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (HasFilesAttr(node.attrs) || BlockMayNeedProjectFiles(node.body)) {
            return true;
          }
          for (const auto& path : node.paths) {
            if (KeyPathMayNeedProjectFiles(path)) {
              return true;
            }
          }
          return false;
        } else {
          return false;
        }
      },
      stmt);
}

bool VariantPayloadMayNeedProjectFiles(const ast::VariantPayload& payload) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::VariantPayloadTuple>) {
          for (const auto& elem : node.elements) {
            if (TypeMayNeedProjectFiles(elem)) {
              return true;
            }
          }
          return false;
        } else {
          for (const auto& field : node.fields) {
            if (HasFilesAttr(field.attrs) ||
                TypeMayNeedProjectFiles(field.type) ||
                ExprMayNeedProjectFiles(field.init_opt)) {
              return true;
            }
          }
          return false;
        }
      },
      payload);
}

bool RecordMemberMayNeedProjectFiles(const ast::RecordMember& member) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::FieldDecl>) {
          return HasFilesAttr(node.attrs) ||
                 TypeMayNeedProjectFiles(node.type) ||
                 ExprMayNeedProjectFiles(node.init_opt);
        } else if constexpr (std::is_same_v<T, ast::MethodDecl>) {
          return HasFilesAttr(node.attrs) ||
                 GenericParamsMayNeedProjectFiles(node.generic_params) ||
                 ReceiverMayNeedProjectFiles(node.receiver) ||
                 ParamsMayNeedProjectFiles(node.params) ||
                 TypeMayNeedProjectFiles(node.return_type_opt) ||
                 ContractMayNeedProjectFiles(node.contract) ||
                 BlockMayNeedProjectFiles(node.body);
        } else {
          return HasFilesAttr(node.attrs) ||
                 TypeMayNeedProjectFiles(node.default_type);
        }
      },
      member);
}

bool StateMemberMayNeedProjectFiles(const ast::StateMember& member) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StateFieldDecl>) {
          return HasFilesAttr(node.attrs) ||
                 TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::StateMethodDecl>) {
          return HasFilesAttr(node.attrs) ||
                 GenericParamsMayNeedProjectFiles(node.generic_params) ||
                 ReceiverMayNeedProjectFiles(node.receiver) ||
                 ParamsMayNeedProjectFiles(node.params) ||
                 TypeMayNeedProjectFiles(node.return_type_opt) ||
                 ContractMayNeedProjectFiles(node.contract) ||
                 BlockMayNeedProjectFiles(node.body);
        } else {
          return HasFilesAttr(node.attrs) ||
                 ParamsMayNeedProjectFiles(node.params) ||
                 BlockMayNeedProjectFiles(node.body);
        }
      },
      member);
}

bool ClassItemMayNeedProjectFiles(const ast::ClassItem& item) {
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ClassFieldDecl> ||
                      std::is_same_v<T, ast::AbstractFieldDecl>) {
          return HasFilesAttr(node.attrs) ||
                 TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::ClassMethodDecl>) {
          return HasFilesAttr(node.attrs) ||
                 GenericParamsMayNeedProjectFiles(node.generic_params) ||
                 ReceiverMayNeedProjectFiles(node.receiver) ||
                 ParamsMayNeedProjectFiles(node.params) ||
                 TypeMayNeedProjectFiles(node.return_type_opt) ||
                 ContractMayNeedProjectFiles(node.contract) ||
                 BlockMayNeedProjectFiles(node.body_opt);
        } else if constexpr (std::is_same_v<T, ast::AssociatedTypeDecl>) {
          return HasFilesAttr(node.attrs) ||
                 TypeMayNeedProjectFiles(node.default_type);
        } else {
          if (HasFilesAttr(node.attrs)) {
            return true;
          }
          for (const auto& field : node.fields) {
            if (HasFilesAttr(field.attrs) ||
                TypeMayNeedProjectFiles(field.type)) {
              return true;
            }
          }
          return false;
        }
      },
      item);
}

bool ItemMayNeedProjectFiles(const ast::ASTItem& item) {
  if (HasFilesAttr(ast::AttrListOf(item))) {
    return true;
  }
  return std::visit(
      [](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          return BindingMayNeedProjectFiles(node.binding);
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                             std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          return GenericParamsMayNeedProjectFiles(node.generic_params) ||
                 ParamsMayNeedProjectFiles(node.params) ||
                 TypeMayNeedProjectFiles(node.return_type_opt) ||
                 ContractMayNeedProjectFiles(node.contract) ||
                 BlockMayNeedProjectFiles(node.body);
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& ext_item : node.items) {
            const auto& proc = std::get<ast::ExternProcDecl>(ext_item);
            if (HasFilesAttr(proc.attrs) ||
                GenericParamsMayNeedProjectFiles(proc.generic_params) ||
                WhereClauseMayNeedProjectFiles(proc.where_clause) ||
                ParamsMayNeedProjectFiles(proc.params) ||
                TypeMayNeedProjectFiles(proc.return_type_opt) ||
                ContractMayNeedProjectFiles(proc.contract)) {
              return true;
            }
            if (proc.foreign_contracts_opt.has_value()) {
              for (const auto& clause : *proc.foreign_contracts_opt) {
                for (const auto& pred : clause.predicates) {
                  if (ExprMayNeedProjectFiles(pred)) {
                    return true;
                  }
                }
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          if (HasDeriveAttr(node.attrs) ||
              GenericParamsMayNeedProjectFiles(node.generic_params) ||
              WhereClauseMayNeedProjectFiles(node.predicate_clause_opt) ||
              (node.invariant_opt.has_value() &&
               ExprMayNeedProjectFiles(node.invariant_opt->predicate))) {
            return true;
          }
          for (const auto& member : node.members) {
            if (RecordMemberMayNeedProjectFiles(member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          if (HasDeriveAttr(node.attrs) ||
              GenericParamsMayNeedProjectFiles(node.generic_params) ||
              WhereClauseMayNeedProjectFiles(node.predicate_clause_opt) ||
              (node.invariant_opt.has_value() &&
               ExprMayNeedProjectFiles(node.invariant_opt->predicate))) {
            return true;
          }
          for (const auto& variant : node.variants) {
            if (variant.payload_opt.has_value() &&
                VariantPayloadMayNeedProjectFiles(*variant.payload_opt)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          if (HasDeriveAttr(node.attrs) ||
              GenericParamsMayNeedProjectFiles(node.generic_params) ||
              WhereClauseMayNeedProjectFiles(node.predicate_clause_opt) ||
              (node.invariant_opt.has_value() &&
               ExprMayNeedProjectFiles(node.invariant_opt->predicate))) {
            return true;
          }
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              if (StateMemberMayNeedProjectFiles(member)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          if (GenericParamsMayNeedProjectFiles(node.generic_params) ||
              WhereClauseMayNeedProjectFiles(node.predicate_clause_opt)) {
            return true;
          }
          for (const auto& class_item : node.items) {
            if (ClassItemMayNeedProjectFiles(class_item)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          return GenericParamsMayNeedProjectFiles(node.generic_params) ||
                 WhereClauseMayNeedProjectFiles(node.predicate_clause_opt) ||
                 TypeMayNeedProjectFiles(node.type);
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          // Derive bodies receive TypeEmitter and may emit declarations that
          // contain ProjectFiles-capable comptime forms not present in Phase 1.
          return true;
        } else {
          return false;
        }
      },
      item);
}

bool ComptimeMayNeedProjectFiles(
    const std::vector<ast::ASTModule>& modules) {
  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      if (ItemMayNeedProjectFiles(item)) {
        return true;
      }
    }
  }
  return false;
}

std::filesystem::path SourceRootForModule(
    const ast::ASTModule& module,
    const ComptimePassOptions& options) {
  if (!module.path.empty()) {
    const auto it = options.source_roots_by_assembly.find(module.path.front());
    if (it != options.source_roots_by_assembly.end()) {
      return it->second;
    }
  }
  if (options.fallback_source_root.has_value()) {
    return *options.fallback_source_root;
  }
  return options.project_root;
}

}  // namespace

ComptimeResult ComptimePass(const std::vector<ast::ASTModule>& modules,
                            const ComptimePassOptions& options) {
  SPEC_RULE("ComptimePass");
  const auto snapshot_start = std::chrono::steady_clock::now();
  const bool needs_project_files = ComptimeMayNeedProjectFiles(modules);
  std::shared_ptr<comptime_internal::ProjectFileSnapshot> project_files;
  if (needs_project_files) {
    project_files =
        comptime_internal::CaptureProjectFileSnapshot(options.project_root);
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - snapshot_start)
            .count();
    LogComptimeProgress("snapshot=captured entries=" +
                        std::to_string(project_files->entries.size()) +
                        " files=" +
                        std::to_string(project_files->captured_file_count) +
                        " dirs=" +
                        std::to_string(project_files->captured_directory_count) +
                        " bytes=" +
                        std::to_string(project_files->captured_byte_count) +
                        " elapsed_ms=" + std::to_string(elapsed_ms));
  } else {
    LogComptimeProgress("snapshot=skipped reason=no-files-attribute");
  }

  ComptimeResult result;
  std::vector<ast::ASTModule> expanded = modules;
  std::vector<const ast::ASTModule*> available_modules;
  available_modules.reserve(modules.size());
  std::vector<std::size_t> processed_modules;
  processed_modules.reserve(modules.size());
  std::size_t next_hygiene = 0;
  for (std::size_t module_index = 0; module_index < modules.size();
       ++module_index) {
    SPEC_RULE("Phase2-Deterministic-Dependency-Order");
    const auto& module = modules[module_index];
    available_modules.clear();
    for (const std::size_t processed_index : processed_modules) {
      available_modules.push_back(&expanded[processed_index]);
    }
    available_modules.push_back(&expanded[module_index]);

    source::ModuleNames available_module_names;
    available_module_names.reserve(available_modules.size());
    for (const ast::ASTModule* available : available_modules) {
      if (available != nullptr) {
        available_module_names.insert(core::StringOfPath(available->path));
      }
    }

    comptime_internal::CtEnv env = comptime_internal::CtEmptyEnv(module);
    env.diags = &result.diags;
    env.project_root = options.project_root;
    env.source_root = SourceRootForModule(module, options);
    env.next_hygiene = next_hygiene;
    env.available_modules = available_modules;
    env.available_module_names = std::move(available_module_names);
    env.files = project_files;

    ast::ASTModule out = module;
    auto expanded_items = comptime_internal::ExpandModuleItems(module.items, env);
    if (!expanded_items.has_value() || core::HasError(result.diags)) {
      return result;
    }
    out.items = std::move(*expanded_items);
    next_hygiene = env.next_hygiene;
    expanded[module_index] = std::move(out);
    processed_modules.push_back(module_index);
  }

  result.modules = std::move(expanded);
  return result;
}

ComptimeResult ComptimePass(const std::vector<ast::ASTModule>& modules,
                            const std::filesystem::path& project_root,
                            const std::filesystem::path& source_root) {
  ComptimePassOptions options;
  options.project_root = project_root;
  options.fallback_source_root = source_root;
  return ComptimePass(modules, options);
}

ComptimeResult ExecuteComptime(const std::vector<ast::ASTModule>& modules,
                               const ComptimePassOptions& options) {
  SPEC_RULE("ExecuteComptime");
  SPEC_RULE("ExecuteComptime-By-ComptimePass");
  return ComptimePass(modules, options);
}

ComptimeResult ExecuteComptime(const std::vector<ast::ASTModule>& modules,
                               const std::filesystem::path& project_root,
                               const std::filesystem::path& source_root) {
  ComptimePassOptions options;
  options.project_root = project_root;
  options.fallback_source_root = source_root;
  return ExecuteComptime(modules, options);
}

}  // namespace cursive::frontend
