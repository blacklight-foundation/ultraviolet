// =============================================================================
// File: 05_codegen/llvm/emit/ir_emit.cpp
// Canonical owner for the moved LLVM emitter implementation slice.
// =============================================================================
#include "ir_instruction_visitor.h"

namespace cursive::codegen {

using namespace emit_detail;

  // T-LLVM-009: Instruction emission
  void LLVMEmitter::EmitIR(const IRPtr &ir)
  {
    if (!ir)
    {
      return;
    }
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder || !builder->GetInsertBlock() ||
        builder->GetInsertBlock()->getTerminator())
    {
      return;
    }

    using Clock = std::chrono::steady_clock;
    const bool ir_perf_enabled = g_ir_proc_perf_ctx != nullptr;
    if (ir_perf_enabled)
    {
      IRNodePerfFrame frame;
      frame.kind_index = ir->node.index();
      frame.start = Clock::now();
      frame.child_ms = 0;
      g_ir_proc_perf_ctx->stack.push_back(frame);
    }



    std::visit(IRInstructionVisitor{*this, *builder}, ir->node);
    if (ir_perf_enabled && g_ir_proc_perf_ctx && !g_ir_proc_perf_ctx->stack.empty())
    {
      const auto end = Clock::now();
      const IRNodePerfFrame frame = g_ir_proc_perf_ctx->stack.back();
      g_ir_proc_perf_ctx->stack.pop_back();

      const long long elapsed_ms = ElapsedMs(frame.start, end);
      long long self_ms = elapsed_ms - frame.child_ms;
      if (self_ms < 0)
      {
        self_ms = 0;
      }

      if (frame.kind_index < g_ir_proc_perf_ctx->buckets.size())
      {
        auto &bucket = g_ir_proc_perf_ctx->buckets[frame.kind_index];
        bucket.count += 1;
        bucket.total_self_ms += self_ms;
        if (self_ms > bucket.max_self_ms)
        {
          bucket.max_self_ms = self_ms;
        }
      }

      if (!g_ir_proc_perf_ctx->stack.empty())
      {
        g_ir_proc_perf_ctx->stack.back().child_ms += elapsed_ms;
      }
    }
  }

} // namespace cursive::codegen
