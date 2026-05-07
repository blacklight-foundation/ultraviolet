#include "05_codegen/llvm/llvm_emit.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

namespace cursive::codegen {

LLVMEmitter::LLVMEmitter(llvm::LLVMContext& ctx,
                         const std::string& module_name,
                         project::TargetProfile profile)
    : context_(ctx),
      module_(std::make_unique<llvm::Module>(module_name, ctx)),
      builder_(std::make_unique<llvm::IRBuilder<>>(ctx)),
      target_profile_(profile) {}

LLVMEmitter::~LLVMEmitter() = default;

std::unique_ptr<llvm::Module> LLVMEmitter::ReleaseModule() {
  return std::move(module_);
}

void LLVMEmitter::PushLoopTargets(llvm::BasicBlock* break_target,
                                  llvm::BasicBlock* continue_target,
                                  llvm::Value* break_value_slot,
                                  analysis::TypeRef break_result_type) {
  loop_break_targets_.push_back(break_target);
  loop_continue_targets_.push_back(continue_target);
  loop_break_value_slots_.push_back(break_value_slot);
  loop_break_result_types_.push_back(break_result_type);
}

void LLVMEmitter::PopLoopTargets() {
  if (!loop_break_targets_.empty()) {
    loop_break_targets_.pop_back();
  }
  if (!loop_continue_targets_.empty()) {
    loop_continue_targets_.pop_back();
  }
  if (!loop_break_value_slots_.empty()) {
    loop_break_value_slots_.pop_back();
  }
  if (!loop_break_result_types_.empty()) {
    loop_break_result_types_.pop_back();
  }
}

llvm::BasicBlock* LLVMEmitter::CurrentLoopBreakTarget() const {
  if (loop_break_targets_.empty()) {
    return nullptr;
  }
  return loop_break_targets_.back();
}

llvm::BasicBlock* LLVMEmitter::CurrentLoopContinueTarget() const {
  if (loop_continue_targets_.empty()) {
    return nullptr;
  }
  return loop_continue_targets_.back();
}

llvm::Value* LLVMEmitter::CurrentLoopBreakValueSlot() const {
  if (loop_break_value_slots_.empty()) {
    return nullptr;
  }
  return loop_break_value_slots_.back();
}

analysis::TypeRef LLVMEmitter::CurrentLoopBreakResultType() const {
  if (loop_break_result_types_.empty()) {
    return nullptr;
  }
  return loop_break_result_types_.back();
}

}  // namespace cursive::codegen
