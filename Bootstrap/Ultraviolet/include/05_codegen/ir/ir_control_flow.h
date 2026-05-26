#pragma once

#include <type_traits>

#include "05_codegen/ir/ir_model.h"

namespace ultraviolet::codegen {

struct IRControlFlowSummary {
  bool may_fallthrough = true;
  bool may_return = false;
  bool may_break = false;
  bool may_continue = false;
  bool may_panic = false;
};

inline void MergeIRControlTransfers(IRControlFlowSummary& dst,
                                    const IRControlFlowSummary& src) {
  dst.may_return = dst.may_return || src.may_return;
  dst.may_break = dst.may_break || src.may_break;
  dst.may_continue = dst.may_continue || src.may_continue;
  dst.may_panic = dst.may_panic || src.may_panic;
}

inline IRControlFlowSummary SummarizeIRControlFlow(const IRPtr& ir);

inline IRControlFlowSummary SequenceIRControlFlow(const IRPtr& first,
                                                  const IRPtr& second) {
  IRControlFlowSummary out = SummarizeIRControlFlow(first);
  if (!out.may_fallthrough) {
    return out;
  }

  IRControlFlowSummary next = SummarizeIRControlFlow(second);
  MergeIRControlTransfers(out, next);
  out.may_fallthrough = next.may_fallthrough;
  return out;
}

inline IRControlFlowSummary SummarizeIRControlFlow(const IRPtr& ir) {
  if (!ir) {
    return {};
  }

  return std::visit(
      [](const auto& node) -> IRControlFlowSummary {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IROpaque>) {
          return {};
        } else if constexpr (std::is_same_v<T, IRSeq>) {
          IRControlFlowSummary out;
          for (const IRPtr& item : node.items) {
            if (!out.may_fallthrough) {
              break;
            }
            IRControlFlowSummary item_flow = SummarizeIRControlFlow(item);
            MergeIRControlTransfers(out, item_flow);
            out.may_fallthrough = item_flow.may_fallthrough;
          }
          return out;
        } else if constexpr (std::is_same_v<T, IRReturn>) {
          return IRControlFlowSummary{
              .may_fallthrough = false,
              .may_return = true,
          };
        } else if constexpr (std::is_same_v<T, IRBreak>) {
          return IRControlFlowSummary{
              .may_fallthrough = false,
              .may_break = true,
          };
        } else if constexpr (std::is_same_v<T, IRContinue>) {
          return IRControlFlowSummary{
              .may_fallthrough = false,
              .may_continue = true,
          };
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          IRControlFlowSummary out = SummarizeIRControlFlow(node.cleanup_ir);
          out.may_fallthrough = false;
          out.may_panic = true;
          out.may_return = true;
          return out;
        } else if constexpr (std::is_same_v<T, IRInitPanicRaise>) {
          IRControlFlowSummary out = SummarizeIRControlFlow(node.cleanup_ir);
          out.may_fallthrough = false;
          out.may_panic = true;
          out.may_return = true;
          return out;
        } else if constexpr (std::is_same_v<T, IRPanicCheck> ||
                             std::is_same_v<T, IRCheckPoison>) {
          return IRControlFlowSummary{
              .may_fallthrough = true,
              .may_return = true,
              .may_panic = true,
          };
        } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
          IRControlFlowSummary out = SummarizeIRControlFlow(node.cleanup_ir);
          out.may_fallthrough = true;
          out.may_return = true;
          out.may_panic = true;
          return out;
        } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
          IRControlFlowSummary out = SummarizeIRControlFlow(node.cleanup_ir);
          out.may_fallthrough = true;
          out.may_return = true;
          out.may_panic = true;
          return out;
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          return SequenceIRControlFlow(node.setup, node.body);
        } else if constexpr (std::is_same_v<T, IRIf>) {
          IRControlFlowSummary then_flow = SummarizeIRControlFlow(node.then_ir);
          IRControlFlowSummary else_flow = SummarizeIRControlFlow(node.else_ir);
          IRControlFlowSummary out;
          out.may_fallthrough =
              then_flow.may_fallthrough || else_flow.may_fallthrough;
          MergeIRControlTransfers(out, then_flow);
          MergeIRControlTransfers(out, else_flow);
          return out;
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          if (node.arms.empty()) {
            return {};
          }
          IRControlFlowSummary out;
          out.may_fallthrough = false;
          for (const IRIfCaseClause& arm : node.arms) {
            IRControlFlowSummary arm_flow =
                SequenceIRControlFlow(arm.body, arm.cleanup_ir);
            out.may_fallthrough =
                out.may_fallthrough || arm_flow.may_fallthrough;
            MergeIRControlTransfers(out, arm_flow);
          }
          return out;
        } else if constexpr (std::is_same_v<T, IRRegion> ||
                             std::is_same_v<T, IRFrame> ||
                             std::is_same_v<T, IRParallel> ||
                             std::is_same_v<T, IRSpecFallback>) {
          return SummarizeIRControlFlow(node.body);
        } else if constexpr (std::is_same_v<T, IRSpawn> ||
                             std::is_same_v<T, IRDispatch>) {
          return SequenceIRControlFlow(node.captured_env, node.body);
        } else if constexpr (std::is_same_v<T, IRSpecLoop>) {
          IRControlFlowSummary out =
              SequenceIRControlFlow(node.snapshot_ir, node.body_ir);
          if (out.may_fallthrough) {
            IRControlFlowSummary rest =
                SequenceIRControlFlow(node.validate_ir, node.commit_ir);
            MergeIRControlTransfers(out, rest);
            out.may_fallthrough = rest.may_fallthrough;
          }
          return out;
        } else if constexpr (std::is_same_v<T, IRRaceReturn> ||
                             std::is_same_v<T, IRRaceYield>) {
          IRControlFlowSummary out;
          out.may_fallthrough = false;
          for (const IRRaceArm& arm : node.arms) {
            IRControlFlowSummary arm_flow =
                SequenceIRControlFlow(arm.async_ir, arm.handler_ir);
            out.may_fallthrough =
                out.may_fallthrough || arm_flow.may_fallthrough;
            MergeIRControlTransfers(out, arm_flow);
          }
          return out;
        } else if constexpr (std::is_same_v<T, IRAll>) {
          IRControlFlowSummary out;
          for (const IRPtr& item : node.async_irs) {
            if (!out.may_fallthrough) {
              break;
            }
            IRControlFlowSummary item_flow = SummarizeIRControlFlow(item);
            MergeIRControlTransfers(out, item_flow);
            out.may_fallthrough = item_flow.may_fallthrough;
          }
          return out;
        } else {
          return {};
        }
      },
      ir->node);
}

inline bool IRFlowMayFallThrough(const IRPtr& ir) {
  return SummarizeIRControlFlow(ir).may_fallthrough;
}

inline bool IRFlowDefinitelyTerminates(const IRPtr& ir) {
  return !IRFlowMayFallThrough(ir);
}

}  // namespace ultraviolet::codegen
