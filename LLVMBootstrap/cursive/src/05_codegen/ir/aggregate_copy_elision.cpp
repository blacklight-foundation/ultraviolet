#include "05_codegen/ir/aggregate_copy_elision.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

namespace {

bool NameMatches(std::string_view name,
                 std::string_view primary,
                 std::string_view stable = {}) {
  return (!primary.empty() && name == primary) ||
         (!stable.empty() && name == stable);
}

bool ContainsIdentifier(std::string_view text, std::string_view name) {
  if (name.empty()) {
    return false;
  }
  for (std::size_t pos = text.find(name); pos != std::string_view::npos;
       pos = text.find(name, pos + name.size())) {
    const bool left_ok =
        pos == 0 ||
        (!std::isalnum(static_cast<unsigned char>(text[pos - 1])) &&
         text[pos - 1] != '_');
    const std::size_t right = pos + name.size();
    const bool right_ok =
        right >= text.size() ||
        (!std::isalnum(static_cast<unsigned char>(text[right])) &&
         text[right] != '_');
    if (left_ok && right_ok) {
      return true;
    }
  }
  return false;
}

bool IsAggregateType(const analysis::TypeRef& type) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    stripped = type;
  }
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<analysis::TypeArray>(stripped->node) ||
         std::holds_alternative<analysis::TypeTuple>(stripped->node) ||
         std::holds_alternative<analysis::TypeUnion>(stripped->node) ||
         std::holds_alternative<analysis::TypePathType>(stripped->node) ||
         std::holds_alternative<analysis::TypeApply>(stripped->node) ||
         std::holds_alternative<analysis::TypeModalState>(stripped->node);
}

bool TypeEquivalent(const analysis::TypeRef& left,
                    const analysis::TypeRef& right) {
  if (!left || !right) {
    return false;
  }
  const auto equiv = analysis::TypeEquiv(left, right);
  return equiv.ok && equiv.equiv;
}

void FlattenLinearSeq(const IRPtr& ir, std::vector<const IR*>& out) {
  if (!ir) {
    return;
  }
  if (const auto* seq = std::get_if<IRSeq>(&ir->node)) {
    for (const IRPtr& item : seq->items) {
      FlattenLinearSeq(item, out);
    }
    return;
  }
  if (const auto* block = std::get_if<IRBlock>(&ir->node)) {
    FlattenLinearSeq(block->setup, out);
    FlattenLinearSeq(block->body, out);
    return;
  }
  if (const auto* region = std::get_if<IRRegion>(&ir->node)) {
    FlattenLinearSeq(region->body, out);
    return;
  }
  if (const auto* frame = std::get_if<IRFrame>(&ir->node)) {
    FlattenLinearSeq(frame->body, out);
    return;
  }
  out.push_back(ir.get());
}

void CollectReturns(const IRPtr& ir, std::vector<const IRReturn*>& out) {
  if (!ir) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IRReturn>) {
          out.push_back(&node);
        } else if constexpr (std::is_same_v<T, IRSeq>) {
          for (const IRPtr& item : node.items) {
            CollectReturns(item, out);
          }
        } else if constexpr (std::is_same_v<T, IRIf>) {
          CollectReturns(node.then_ir, out);
          CollectReturns(node.else_ir, out);
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          CollectReturns(node.setup, out);
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          CollectReturns(node.iter_ir, out);
          CollectReturns(node.cond_ir, out);
          CollectReturns(node.body_ir, out);
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          for (const IRIfCaseClause& arm : node.arms) {
            CollectReturns(arm.body, out);
            CollectReturns(arm.cleanup_ir, out);
          }
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          CollectReturns(node.cleanup_ir, out);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          CollectReturns(node.captured_env, out);
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          CollectReturns(node.body, out);
          CollectReturns(node.captured_env, out);
        } else if constexpr (std::is_same_v<T, IRSpecFallback>) {
          CollectReturns(node.body, out);
        } else if constexpr (std::is_same_v<T, IRSpecLoop>) {
          CollectReturns(node.snapshot_ir, out);
          CollectReturns(node.body_ir, out);
          CollectReturns(node.validate_ir, out);
          CollectReturns(node.commit_ir, out);
          CollectReturns(node.retry_ir, out);
          CollectReturns(node.fallback_ir, out);
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          for (const IRRaceArm& arm : node.arms) {
            CollectReturns(arm.async_ir, out);
            CollectReturns(arm.handler_ir, out);
          }
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          for (const IRRaceArm& arm : node.arms) {
            CollectReturns(arm.async_ir, out);
            CollectReturns(arm.handler_ir, out);
          }
        } else if constexpr (std::is_same_v<T, IRAll>) {
          for (const IRPtr& item : node.async_irs) {
            CollectReturns(item, out);
          }
        }
      },
      ir->node);
}

bool ValueRefsName(const IRValue& value,
                   const LowerCtx& ctx,
                   std::string_view primary,
                   std::string_view stable,
                   std::string_view ignore_primary,
                   std::string_view ignore_stable,
                   std::unordered_set<std::string>& visiting) {
  if (value.kind == IRValue::Kind::Local &&
      NameMatches(value.name, ignore_primary, ignore_stable)) {
    return false;
  }
  if (value.kind == IRValue::Kind::Local &&
      NameMatches(value.name, primary, stable)) {
    return true;
  }

  const DerivedValueInfo* derived = ctx.LookupDerivedValue(value);
  if (!derived) {
    return false;
  }
  if (!value.name.empty() && !visiting.insert(value.name).second) {
    return false;
  }

  auto refs = [&](const IRValue& candidate) {
    return ValueRefsName(candidate,
                         ctx,
                         primary,
                         stable,
                         ignore_primary,
                         ignore_stable,
                         visiting);
  };
  auto refs_range = [&](const IRRange& range) {
    return (range.lo.has_value() && refs(*range.lo)) ||
           (range.hi.has_value() && refs(*range.hi));
  };

  if (refs(derived->base) ||
      refs(derived->index) ||
      refs_range(derived->range) ||
      (derived->range_value.has_value() && refs(*derived->range_value)) ||
      refs(derived->repeat_value) ||
      refs(derived->repeat_count)) {
    return true;
  }
  for (const IRValue& element : derived->elements) {
    if (refs(element)) {
      return true;
    }
  }
  for (const DerivedArraySegment& segment : derived->array_segments) {
    if (refs(segment.value) ||
        (segment.count.has_value() && refs(*segment.count))) {
      return true;
    }
  }
  for (const auto& field : derived->fields) {
    if (refs(field.second)) {
      return true;
    }
  }
  for (const IRValue& payload : derived->payload_elems) {
    if (refs(payload)) {
      return true;
    }
  }
  for (const auto& field : derived->payload_fields) {
    if (refs(field.second)) {
      return true;
    }
  }
  return false;
}

bool ValueRefsName(const IRValue& value,
                   const LowerCtx& ctx,
                   std::string_view primary,
                   std::string_view stable = {},
                   std::string_view ignore_primary = {},
                   std::string_view ignore_stable = {}) {
  std::unordered_set<std::string> visiting;
  return ValueRefsName(value,
                       ctx,
                       primary,
                       stable,
                       ignore_primary,
                       ignore_stable,
                       visiting);
}

bool PlaceRefsName(const IRPlace& place,
                   std::string_view primary,
                   std::string_view stable = {}) {
  return ContainsIdentifier(place.repr, primary) ||
         (!stable.empty() && ContainsIdentifier(place.repr, stable));
}

bool NodeRefsName(const IR& ir,
                  const LowerCtx& ctx,
                  std::string_view primary,
                  std::string_view stable = {},
                  std::string_view ignore_primary = {},
                  std::string_view ignore_stable = {});

bool IrRefsName(const IRPtr& ir,
                const LowerCtx& ctx,
                std::string_view primary,
                std::string_view stable = {},
                std::string_view ignore_primary = {},
                std::string_view ignore_stable = {}) {
  return ir &&
         NodeRefsName(*ir,
                      ctx,
                      primary,
                      stable,
                      ignore_primary,
                      ignore_stable);
}

bool NodeRefsName(const IR& ir,
                  const LowerCtx& ctx,
                  std::string_view primary,
                  std::string_view stable,
                  std::string_view ignore_primary,
                  std::string_view ignore_stable) {
  auto refs = [&](const IRValue& value) {
    return ValueRefsName(value,
                         ctx,
                         primary,
                         stable,
                         ignore_primary,
                         ignore_stable);
  };
  auto refs_range = [&](const IRRange& range) {
    return (range.lo.has_value() && refs(*range.lo)) ||
           (range.hi.has_value() && refs(*range.hi));
  };
  auto refs_many = [&](const std::vector<IRValue>& values) {
    return std::any_of(values.begin(), values.end(), refs);
  };
  auto refs_ir = [&](const IRPtr& nested) {
    return IrRefsName(nested,
                      ctx,
                      primary,
                      stable,
                      ignore_primary,
                      ignore_stable);
  };

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IRSeq>) {
          for (const IRPtr& item : node.items) {
            if (refs_ir(item)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, IRCall>) {
          return refs(node.callee) || refs_many(node.args);
        } else if constexpr (std::is_same_v<T, IRCallVTable>) {
          return refs(node.base) || refs_many(node.args);
        } else if constexpr (std::is_same_v<T, IRStoreGlobal>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRReadVar>) {
          return NameMatches(node.name, primary, stable);
        } else if constexpr (std::is_same_v<T, IRBindVar>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRStoreVar>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRStoreVarNoDrop>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRReadPlace>) {
          return PlaceRefsName(node.place, primary, stable);
        } else if constexpr (std::is_same_v<T, IRWritePlace>) {
          return PlaceRefsName(node.place, primary, stable) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRAddrOf>) {
          return PlaceRefsName(node.place, primary, stable) ||
                 std::any_of(node.ref_syms.begin(),
                             node.ref_syms.end(),
                             [&](const std::string& name) {
                               return NameMatches(name, primary, stable);
                             });
        } else if constexpr (std::is_same_v<T, IRReadPtr>) {
          return refs(node.ptr);
        } else if constexpr (std::is_same_v<T, IRWritePtr>) {
          return refs(node.ptr) || refs(node.value);
        } else if constexpr (std::is_same_v<T, IRUnaryOp>) {
          return refs(node.operand);
        } else if constexpr (std::is_same_v<T, IRBinaryOp>) {
          return refs(node.lhs) || refs(node.rhs);
        } else if constexpr (std::is_same_v<T, IRCast>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRTransmute>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRCheckIndex>) {
          return refs(node.base) || refs(node.index);
        } else if constexpr (std::is_same_v<T, IRCheckRange>) {
          return refs(node.base) || refs_range(node.range) ||
                 (node.range_value.has_value() && refs(*node.range_value));
        } else if constexpr (std::is_same_v<T, IRCheckSliceLen>) {
          return refs(node.base) || refs_range(node.range) ||
                 (node.range_value.has_value() && refs(*node.range_value)) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRCheckOp>) {
          return refs(node.lhs) ||
                 (node.rhs.has_value() && refs(*node.rhs));
        } else if constexpr (std::is_same_v<T, IRCheckCast>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRAlloc>) {
          return (node.region.has_value() && refs(*node.region)) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRContextBundleBuild>) {
          return refs(node.root_ctx);
        } else if constexpr (std::is_same_v<T, IRReturn>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRResult>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRBreak>) {
          return node.value.has_value() && refs(*node.value);
        } else if constexpr (std::is_same_v<T, IRMoveState>) {
          return PlaceRefsName(node.place, primary, stable);
        } else if constexpr (std::is_same_v<T, IRIf>) {
          return refs(node.cond) ||
                 refs_ir(node.then_ir) ||
                 refs(node.then_value) ||
                 refs_ir(node.else_ir) ||
                 refs(node.else_value);
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          return refs_ir(node.setup) ||
                 refs_ir(node.body) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          return refs_ir(node.iter_ir) ||
                 (node.iter_value.has_value() && refs(*node.iter_value)) ||
                 refs_ir(node.cond_ir) ||
                 (node.cond_value.has_value() && refs(*node.cond_value)) ||
                 refs_ir(node.body_ir) ||
                 refs(node.body_value);
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          if (refs(node.scrutinee)) {
            return true;
          }
          for (const IRIfCaseClause& arm : node.arms) {
            if (refs_ir(arm.body) ||
                refs_ir(arm.cleanup_ir) ||
                refs(arm.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          return refs(node.owner) ||
                 refs_ir(node.body) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          return (node.region.has_value() && refs(*node.region)) ||
                 refs_ir(node.body) ||
                 refs(node.value);
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          return refs_ir(node.cleanup_ir);
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          return refs_ir(node.cleanup_ir);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          return refs(node.domain) ||
                 refs_ir(node.body) ||
                 refs(node.result) ||
                 (node.cancel_token.has_value() && refs(*node.cancel_token));
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          return refs_ir(node.captured_env) ||
                 refs_ir(node.body) ||
                 refs(node.body_result) ||
                 refs(node.env_ptr) ||
                 refs(node.env_size) ||
                 refs(node.body_fn) ||
                 refs(node.result_size) ||
                 (node.affinity_mask.has_value() &&
                  refs(*node.affinity_mask)) ||
                 (node.priority.has_value() && refs(*node.priority));
        } else if constexpr (std::is_same_v<T, IRWait>) {
          return refs(node.handle);
        } else if constexpr (std::is_same_v<T, IRCancelCheck>) {
          return refs(node.token);
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          return refs(node.range) ||
                 refs_ir(node.body) ||
                 refs(node.body_result) ||
                 refs_ir(node.captured_env) ||
                 refs(node.env_ptr) ||
                 refs(node.body_fn) ||
                 refs(node.elem_size) ||
                 refs(node.result_size) ||
                 refs(node.result_ptr) ||
                 (node.reduce_fn.has_value() && refs(*node.reduce_fn)) ||
                 (node.chunk_size.has_value() && refs(*node.chunk_size));
        } else if constexpr (std::is_same_v<T, IRYield>) {
          return refs(node.value) ||
                 refs(node.result) ||
                 refs(node.keys_record);
        } else if constexpr (std::is_same_v<T, IRYieldFrom>) {
          return refs(node.source) || refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSpecSnapshot>) {
          return refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSpecValidate>) {
          return refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSpecCommit>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRSpecRetry>) {
          return refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSpecFallback>) {
          return refs_ir(node.body) ||
                 refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSpecLoop>) {
          return refs_ir(node.snapshot_ir) ||
                 refs_ir(node.body_ir) ||
                 refs_ir(node.validate_ir) ||
                 refs_ir(node.commit_ir) ||
                 refs_ir(node.retry_ir) ||
                 refs_ir(node.fallback_ir) ||
                 refs(node.result);
        } else if constexpr (std::is_same_v<T, IRSync>) {
          return refs(node.async_value);
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          for (const IRRaceArm& arm : node.arms) {
            if (refs_ir(arm.async_ir) ||
                refs(arm.async_value) ||
                refs(arm.match_value) ||
                refs_ir(arm.handler_ir) ||
                refs(arm.handler_result)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          for (const IRRaceArm& arm : node.arms) {
            if (refs_ir(arm.async_ir) ||
                refs(arm.async_value) ||
                refs(arm.match_value) ||
                refs_ir(arm.handler_ir) ||
                refs(arm.handler_result)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, IRAll>) {
          for (const IRPtr& item : node.async_irs) {
            if (refs_ir(item)) {
              return true;
            }
          }
          return refs_many(node.async_values) || refs(node.result);
        } else if constexpr (std::is_same_v<T, IRAsyncComplete>) {
          return refs(node.value);
        } else if constexpr (std::is_same_v<T, IRAsyncFail>) {
          return refs(node.value);
        } else {
          return false;
        }
      },
      ir.node);
}

bool NodeDefinesName(const IR& ir,
                     std::string_view primary,
                     std::string_view stable = {}) {
  if (const auto* bind = std::get_if<IRBindVar>(&ir.node)) {
    return NameMatches(bind->name, primary, stable) ||
           NameMatches(bind->stable_name, primary, stable);
  }
  return false;
}

bool NodeWritesName(const IR& ir,
                    std::string_view primary,
                    std::string_view stable = {}) {
  if (const auto* store = std::get_if<IRStoreVar>(&ir.node)) {
    return NameMatches(store->name, primary, stable);
  }
  if (const auto* store = std::get_if<IRStoreVarNoDrop>(&ir.node)) {
    return NameMatches(store->name, primary, stable);
  }
  if (const auto* write = std::get_if<IRWritePlace>(&ir.node)) {
    return PlaceRefsName(write->place, primary, stable);
  }
  return false;
}

}  // namespace

std::optional<IRAggregateCopyElision> AnalyzeAggregateCopyElision(
    const ProcIR& proc,
    const LowerCtx& ctx) {
  if (!proc.ret || !IsAggregateType(proc.ret)) {
    return std::nullopt;
  }

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (!analysis::BitcopyType(scope, proc.ret)) {
    return std::nullopt;
  }

  std::vector<const IRReturn*> returns;
  CollectReturns(proc.body, returns);
  if (returns.size() != 1) {
    return std::nullopt;
  }
  const IRValue& return_value = returns.front()->value;
  if (return_value.kind != IRValue::Kind::Local || return_value.name.empty()) {
    return std::nullopt;
  }

  std::vector<const IR*> linear;
  FlattenLinearSeq(proc.body, linear);

  const IRBindVar* return_bind = nullptr;
  std::size_t bind_index = 0;
  for (std::size_t i = 0; i < linear.size(); ++i) {
    const auto* bind = std::get_if<IRBindVar>(&linear[i]->node);
    if (!bind) {
      continue;
    }
    if (!NameMatches(return_value.name, bind->name, bind->stable_name)) {
      continue;
    }
    if (bind->value.kind != IRValue::Kind::Local || bind->value.name.empty()) {
      return std::nullopt;
    }
    return_bind = bind;
    bind_index = i;
    break;
  }
  if (!return_bind || !TypeEquivalent(return_bind->type, proc.ret)) {
    return std::nullopt;
  }

  const IRParam* source_param = nullptr;
  std::size_t source_param_index = 0;
  for (std::size_t i = 0; i < proc.params.size(); ++i) {
    const IRParam& param = proc.params[i];
    if (param.mode.has_value()) {
      continue;
    }
    if (!NameMatches(return_bind->value.name, param.name, param.stable_name)) {
      continue;
    }
    if (!TypeEquivalent(param.type, proc.ret)) {
      return std::nullopt;
    }
    source_param = &param;
    source_param_index = i;
    break;
  }
  if (!source_param) {
    return std::nullopt;
  }

  for (std::size_t i = bind_index + 1; i < linear.size(); ++i) {
    const IR& node = *linear[i];
    if (NodeDefinesName(node,
                        return_bind->name,
                        return_bind->stable_name)) {
      return std::nullopt;
    }
    if (NodeWritesName(node,
                       source_param->name,
                       source_param->stable_name) ||
        NodeRefsName(node,
                     ctx,
                     source_param->name,
                     source_param->stable_name,
                     return_bind->name,
                     return_bind->stable_name)) {
      return std::nullopt;
    }
  }

  IRAggregateCopyElision info;
  info.return_local_uses_sret = true;
  info.return_local = return_bind->name;
  info.return_local_stable_name = return_bind->stable_name;
  info.source_param = source_param->name;
  info.source_param_stable_name = source_param->stable_name;
  info.source_param_index = source_param_index;
  return info;
}

}  // namespace cursive::codegen
