// =============================================================================
// callgraph_caps.cpp - Call Graph Construction and Capability Flow Analysis
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 19.1 "Capability Flow Analysis" (lines 24210-24300)
//   - Section 19.2 "Call Graph Construction" (lines 24310-24400)
//
// SOURCE FILE:
//   - cursive-bootstrap/src/03_analysis/caps/cap_concurrency.cpp (call graph)
//
// FUNCTIONS IMPLEMENTED:
//   - BuildCallGraph() - Construct call graph for module
//   - AnnotateCapabilityFlow() - Annotate edges with capability flow
//   - DetectCapabilityLeaks() - Find capabilities escaping to extern
//   - ValidateCapabilityChain() - Verify capability flow from entry
//   - ComputeTransitiveClosure() - Compute transitive call relationships
//   - PropagateCapabilityRequirements() - Propagate through call graph
//
// =============================================================================

#include "04_analysis/caps/callgraph_caps.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <string_view>
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCallGraphCaps() {
  SPEC_DEF("CallGraphConstruction", "19.2");
  SPEC_DEF("CapabilityFlowAnalysis", "19.1");
  SPEC_DEF("CapabilityLeakDetection", "19.1");
  SPEC_DEF("TransitiveClosure", "19.2");
}

std::string ModulePathToString(const ast::Path& path) {
  std::string module_path;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      module_path += "::";
    }
    module_path += path[i];
  }
  return module_path;
}

ast::ModulePath ModulePathFromString(std::string_view module_path) {
  ast::ModulePath out;
  if (module_path.empty()) {
    return out;
  }

  std::size_t start = 0;
  while (start < module_path.size()) {
    const std::size_t sep = module_path.find("::", start);
    if (sep == std::string_view::npos) {
      out.emplace_back(module_path.substr(start));
      break;
    }
    out.emplace_back(module_path.substr(start, sep - start));
    start = sep + 2;
  }
  return out;
}

struct CollectedCallTarget {
  std::optional<std::string> module_path;
  std::string name;
  core::Span call_span;
  const ast::Expr* callee_expr = nullptr;
};

struct CollectedUnresolvedDirectCall {
  const ast::Expr* callee_expr = nullptr;
  core::Span call_span;
};

struct CollectedUnresolvedMethodCall {
  const ast::Expr* receiver_expr = nullptr;
  std::vector<const ast::Expr*> arg_exprs;
  std::string method_name;
  core::Span call_span;
};

void AddCallTarget(std::vector<CollectedCallTarget>& targets,
                   std::optional<std::string> module_path,
                   std::string name,
                   const core::Span& call_span,
                   const ast::Expr* callee_expr) {
  CollectedCallTarget target;
  target.module_path = std::move(module_path);
  target.name = std::move(name);
  target.call_span = call_span;
  target.callee_expr = callee_expr;
  targets.push_back(std::move(target));
}

void AddUnresolvedDirectCall(
    std::vector<CollectedUnresolvedDirectCall>& unresolved_calls,
    const core::Span& call_span,
    const ast::Expr* callee_expr) {
  if (!callee_expr) {
    return;
  }

  CollectedUnresolvedDirectCall call;
  call.call_span = call_span;
  call.callee_expr = callee_expr;
  unresolved_calls.push_back(call);
}

void AddUnresolvedMethodCall(
    std::vector<CollectedUnresolvedMethodCall>& unresolved_method_calls,
    const ast::MethodCallExpr& method_call,
    const core::Span& call_span) {
  if (!method_call.receiver) {
    return;
  }

  CollectedUnresolvedMethodCall call;
  call.receiver_expr = method_call.receiver.get();
  call.method_name = method_call.name;
  call.call_span = call_span;
  for (const auto& arg : method_call.args) {
    if (arg.value) {
      call.arg_exprs.push_back(arg.value.get());
    }
  }
  unresolved_method_calls.push_back(std::move(call));
}

void CollectCallTargetsFromApplyArgs(
    const ast::ApplyArgs& args,
    std::vector<CollectedCallTarget>& targets,
    std::vector<CollectedUnresolvedDirectCall>& unresolved_calls,
    std::vector<CollectedUnresolvedMethodCall>& unresolved_method_calls);

/// Extract call targets from an expression.
void CollectCallTargets(const ast::Expr& expr,
                        std::vector<CollectedCallTarget>& targets,
                        std::vector<CollectedUnresolvedDirectCall>&
                            unresolved_calls,
                        std::vector<CollectedUnresolvedMethodCall>&
                            unresolved_method_calls);

/// Extract call targets from a statement
void CollectCallTargetsFromStmt(const ast::Stmt& stmt,
                                std::vector<CollectedCallTarget>& targets,
                                std::vector<CollectedUnresolvedDirectCall>&
                                    unresolved_calls,
                                std::vector<CollectedUnresolvedMethodCall>&
                                    unresolved_method_calls);

/// Extract call targets from a block
void CollectCallTargetsFromBlock(const ast::Block& block,
                                 std::vector<CollectedCallTarget>& targets,
                                 std::vector<CollectedUnresolvedDirectCall>&
                                     unresolved_calls,
                                 std::vector<CollectedUnresolvedMethodCall>&
                                     unresolved_method_calls);

void CollectCallTargetsFromApplyArgs(
    const ast::ApplyArgs& args,
    std::vector<CollectedCallTarget>& targets,
    std::vector<CollectedUnresolvedDirectCall>& unresolved_calls,
    std::vector<CollectedUnresolvedMethodCall>& unresolved_method_calls) {
  std::visit(
      [&targets, &unresolved_calls, &unresolved_method_calls](
          const auto& apply_args) {
        using T = std::decay_t<decltype(apply_args)>;

        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          for (const auto& arg : apply_args.args) {
            if (arg.value) {
              CollectCallTargets(*arg.value, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::BraceArgs>) {
          for (const auto& field : apply_args.fields) {
            if (field.value) {
              CollectCallTargets(*field.value, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }
        }
      },
      args);
}

void CollectCallTargets(const ast::Expr& expr,
                        std::vector<CollectedCallTarget>& targets,
                        std::vector<CollectedUnresolvedDirectCall>&
                            unresolved_calls,
                        std::vector<CollectedUnresolvedMethodCall>&
                            unresolved_method_calls) {
  std::visit(
      [&expr, &targets, &unresolved_calls, &unresolved_method_calls](
          const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          // Direct call: preserve qualification for path-based callees.
          if (node.callee) {
            if (const auto* path =
                    std::get_if<ast::PathExpr>(&node.callee->node)) {
              std::optional<std::string> target_module_path;
              if (!path->path.empty()) {
                target_module_path = ModulePathToString(path->path);
              }
              AddCallTarget(targets, std::move(target_module_path), path->name,
                            expr.span, node.callee.get());
            } else if (const auto* qname = std::get_if<ast::QualifiedNameExpr>(
                           &node.callee->node)) {
              std::optional<std::string> target_module_path;
              if (!qname->path.empty()) {
                target_module_path = ModulePathToString(qname->path);
              }
              AddCallTarget(targets, std::move(target_module_path), qname->name,
                            expr.span, node.callee.get());
            } else if (const auto* ident =
                           std::get_if<ast::IdentifierExpr>(&node.callee->node)) {
              AddCallTarget(targets, std::nullopt, ident->name, expr.span,
                            node.callee.get());
            } else {
              AddUnresolvedDirectCall(unresolved_calls, expr.span,
                                      node.callee.get());
              CollectCallTargets(*node.callee, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }

          // Recurse into arguments
          for (const auto& arg : node.args) {
            if (arg.value) {
              CollectCallTargets(*arg.value, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          std::optional<std::string> target_module_path;
          if (!node.path.empty()) {
            target_module_path = ModulePathToString(node.path);
          }
          AddCallTarget(targets, std::move(target_module_path), node.name,
                        expr.span, nullptr);
          CollectCallTargetsFromApplyArgs(node.args, targets, unresolved_calls,
                                          unresolved_method_calls);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          AddUnresolvedMethodCall(unresolved_method_calls, node, expr.span);
          // Method calls are not resolved as global procedure calls here.
          if (node.receiver) {
            CollectCallTargets(*node.receiver, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          for (const auto& arg : node.args) {
            if (arg.value) {
              CollectCallTargets(*arg.value, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            CollectCallTargetsFromBlock(*node.block, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          if (node.cond) {
            CollectCallTargets(*node.cond, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.then_expr) {
            CollectCallTargets(*node.then_expr, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.else_expr) {
            CollectCallTargets(*node.else_expr, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (node.scrutinee) {
            CollectCallTargets(*node.scrutinee, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          for (const auto& case_clause : node.cases) {
            if (case_clause.body) {
              CollectCallTargets(*case_clause.body, targets, unresolved_calls,
                                 unresolved_method_calls);
            }
          }
          if (node.else_expr) {
            CollectCallTargets(*node.else_expr, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          if (node.scrutinee) {
            CollectCallTargets(*node.scrutinee, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.then_expr) {
            CollectCallTargets(*node.then_expr, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.else_expr) {
            CollectCallTargets(*node.else_expr, targets, unresolved_calls,
                                 unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          if (node.cond) {
            CollectCallTargets(*node.cond, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          if (node.iter) {
            CollectCallTargets(*node.iter, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          if (node.lhs) {
            CollectCallTargets(*node.lhs, targets, unresolved_calls,
                               unresolved_method_calls);
          }
          if (node.rhs) {
            CollectCallTargets(*node.rhs, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          if (node.value) {
            CollectCallTargets(*node.value, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        }
        // Add more expression types as needed
      },
      expr.node);
}

void CollectCallTargetsFromStmt(const ast::Stmt& stmt,
                                std::vector<CollectedCallTarget>& targets,
                                std::vector<CollectedUnresolvedDirectCall>&
                                    unresolved_calls,
                                std::vector<CollectedUnresolvedMethodCall>&
                                    unresolved_method_calls) {
  std::visit(
      [&targets, &unresolved_calls, &unresolved_method_calls](
          const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          if (node.binding.init) {
            CollectCallTargets(*node.binding.init, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          if (node.binding.init) {
            CollectCallTargets(*node.binding.init, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          if (node.value) {
            CollectCallTargets(*node.value, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          if (node.value) {
            CollectCallTargets(*node.value, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          if (node.value_opt) {
            CollectCallTargets(*node.value_opt, targets, unresolved_calls,
                               unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            CollectCallTargetsFromBlock(*node.body, targets, unresolved_calls,
                                        unresolved_method_calls);
          }
        }
      },
      stmt);  // Stmt IS the variant, not stmt.node
}

void CollectCallTargetsFromBlock(const ast::Block& block,
                                 std::vector<CollectedCallTarget>& targets,
                                 std::vector<CollectedUnresolvedDirectCall>&
                                     unresolved_calls,
                                 std::vector<CollectedUnresolvedMethodCall>&
                                     unresolved_method_calls) {
  for (const auto& stmt : block.stmts) {
    // Stmt is a variant, not a pointer - iterate directly
    CollectCallTargetsFromStmt(stmt, targets, unresolved_calls,
                               unresolved_method_calls);
  }
  if (block.tail_opt) {
    CollectCallTargets(*block.tail_opt, targets, unresolved_calls,
                       unresolved_method_calls);
  }
}

std::optional<ProcId> ResolveSingleModuleCallee(
    const CollectedCallTarget& target,
    const std::string& caller_module_path,
    const CallGraph& cg) {
  ProcId callee = {target.module_path.value_or(caller_module_path), target.name};
  if (!cg.HasNode(callee)) {
    return std::nullopt;
  }
  return callee;
}

std::optional<ProcId> ResolveMultiModuleCallee(
    const CollectedCallTarget& target,
    const std::string& caller_module_path,
    const std::vector<const ast::ASTModule*>& modules,
    const CallGraph& cg) {
  if (target.module_path.has_value()) {
    ProcId qualified = {*target.module_path, target.name};
    if (cg.HasNode(qualified)) {
      return qualified;
    }
    return std::nullopt;
  }

  const ProcId same_module = {caller_module_path, target.name};
  if (cg.HasNode(same_module)) {
    return same_module;
  }

  std::optional<ProcId> callee;
  bool ambiguous = false;
  for (const ast::ASTModule* other : modules) {
    if (!other) {
      continue;
    }
    std::string other_path = ModulePathToString(other->path);
    ProcId candidate = {other_path, target.name};
    if (!cg.HasNode(candidate)) {
      continue;
    }
    if (!callee.has_value()) {
      callee = candidate;
    } else if (!(callee->module_path == candidate.module_path &&
                 callee->name == candidate.name)) {
      ambiguous = true;
      break;
    }
  }

  if (ambiguous) {
    return std::nullopt;
  }
  return callee;
}

CapabilitySet InferCallableParamCapabilities(
    const TypeRef& callee_type,
    const ScopeContext* ctx = nullptr,
    const ast::ModulePath* current_module = nullptr) {
  auto infer_caps = [&](const TypeRef& type) {
    if (ctx && current_module) {
      return InferCapabilitiesFromType(*ctx, *current_module, type);
    }
    return InferCapabilitiesFromType(type);
  };

  if (!callee_type) {
    return CapabilitySet::Empty();
  }

  if (const auto* perm = std::get_if<TypePerm>(&callee_type->node)) {
    return InferCallableParamCapabilities(perm->base, ctx, current_module);
  }
  if (const auto* refine = std::get_if<TypeRefine>(&callee_type->node)) {
    return InferCallableParamCapabilities(refine->base, ctx, current_module);
  }
  if (const auto* func = std::get_if<TypeFunc>(&callee_type->node)) {
    CapabilitySet required = CapabilitySet::Empty();
    for (const auto& param : func->params) {
      required = required.Union(infer_caps(param.type));
    }
    return required;
  }
  if (const auto* closure = std::get_if<TypeClosure>(&callee_type->node)) {
    CapabilitySet required = CapabilitySet::Empty();
    for (const auto& param : closure->params) {
      required = required.Union(infer_caps(param.second));
    }
    return required;
  }
  if (const auto* union_type = std::get_if<TypeUnion>(&callee_type->node)) {
    CapabilitySet required = CapabilitySet::Empty();
    for (const auto& member : union_type->members) {
      required =
          required.Union(InferCallableParamCapabilities(member, ctx, current_module));
    }
    return required;
  }
  return CapabilitySet::Empty();
}

}  // namespace

// =============================================================================
// CallGraph implementation
// =============================================================================

void CallGraph::AddNode(CallGraphNode node) {
  ProcId id = node.id;
  nodes_[id] = std::move(node);
}

void CallGraph::AddEdge(CallGraphEdge edge) {
  std::size_t edge_idx = edges_.size();
  outgoing_edges_[edge.caller].push_back(edge_idx);
  incoming_edges_[edge.callee].push_back(edge_idx);
  edges_.push_back(std::move(edge));
}

void CallGraph::AddUnresolvedDirectCall(UnresolvedDirectCall call) {
  if (!call.callee_expr) {
    return;
  }
  unresolved_direct_calls_.push_back(std::move(call));
}

void CallGraph::AddUnresolvedMethodCall(UnresolvedMethodCall call) {
  if (!call.receiver_expr) {
    return;
  }
  unresolved_method_calls_.push_back(std::move(call));
}

const CallGraphNode* CallGraph::GetNode(const ProcId& id) const {
  auto it = nodes_.find(id);
  return it != nodes_.end() ? &it->second : nullptr;
}

const std::unordered_map<ProcId, CallGraphNode, ProcIdHash>&
CallGraph::GetNodes() const {
  return nodes_;
}

std::unordered_map<ProcId, CallGraphNode, ProcIdHash>&
CallGraph::GetMutableNodes() {
  return nodes_;
}

std::vector<const CallGraphEdge*> CallGraph::GetOutgoingEdges(
    const ProcId& caller) const {
  std::vector<const CallGraphEdge*> result;
  auto it = outgoing_edges_.find(caller);
  if (it != outgoing_edges_.end()) {
    for (std::size_t idx : it->second) {
      result.push_back(&edges_[idx]);
    }
  }
  return result;
}

std::vector<const CallGraphEdge*> CallGraph::GetIncomingEdges(
    const ProcId& callee) const {
  std::vector<const CallGraphEdge*> result;
  auto it = incoming_edges_.find(callee);
  if (it != incoming_edges_.end()) {
    for (std::size_t idx : it->second) {
      result.push_back(&edges_[idx]);
    }
  }
  return result;
}

const std::vector<CallGraphEdge>& CallGraph::GetEdges() const {
  return edges_;
}

std::vector<CallGraphEdge>& CallGraph::GetMutableEdges() {
  return edges_;
}

const std::vector<UnresolvedDirectCall>& CallGraph::GetUnresolvedDirectCalls()
    const {
  return unresolved_direct_calls_;
}

const std::vector<UnresolvedMethodCall>& CallGraph::GetUnresolvedMethodCalls()
    const {
  return unresolved_method_calls_;
}

bool CallGraph::HasNode(const ProcId& id) const {
  return nodes_.find(id) != nodes_.end();
}

std::vector<const CallGraphNode*> CallGraph::GetExternNodes() const {
  std::vector<const CallGraphNode*> result;
  for (const auto& [id, node] : nodes_) {
    if (node.is_extern) {
      result.push_back(&node);
    }
  }
  return result;
}

const CallGraphNode* CallGraph::GetEntryPoint() const {
  for (const auto& [id, node] : nodes_) {
    if (IdEq(id.name, "main") && !node.is_extern) {
      return &node;
    }
  }
  return nullptr;
}

// =============================================================================
// Call graph construction
// =============================================================================

namespace {

std::string RecordMethodNodeName(std::string_view record_name,
                                 std::string_view method_name) {
  return std::string(record_name) + "::" + std::string(method_name);
}

std::string StateMethodNodeName(std::string_view modal_name,
                                std::string_view state_name,
                                std::string_view method_name) {
  return std::string(modal_name) + "@" + std::string(state_name) + "::" +
         std::string(method_name);
}

std::string TransitionNodeName(std::string_view modal_name,
                               std::string_view state_name,
                               std::string_view transition_name) {
  return std::string(modal_name) + "@" + std::string(state_name) + "->" +
         std::string(transition_name);
}

CallGraphNode MakeCallableNode(const std::string& module_path,
                               std::string callable_name,
                               CapabilitySignature cap_sig,
                               const core::Span& span) {
  CallGraphNode node;
  node.id = {module_path, std::move(callable_name)};
  node.is_extern = false;
  node.cap_sig = std::move(cap_sig);
  node.span = span;
  return node;
}

CallGraphNode MakeExternNode(const std::string& module_path,
                             const ast::ExternProcDecl& extern_proc) {
  CallGraphNode node;
  node.id = {module_path, extern_proc.name};
  node.is_extern = true;
  node.span = extern_proc.span;
  node.cap_sig.required = CapabilitySet::Empty();
  node.cap_sig.provided = CapabilitySet::Empty();
  return node;
}

CapabilitySignature ProcCapabilitySignature(const ScopeContext* ctx,
                                            const ast::ModulePath& module_path,
                                            const ast::ProcedureDecl& proc) {
  if (ctx) {
    return BuildCapabilitySignature(*ctx, module_path, proc);
  }
  return BuildCapabilitySignature(proc);
}

CapabilitySignature MethodCapabilitySignature(
    const ScopeContext* ctx,
    const ast::ModulePath& module_path,
    const std::string& owner_name,
    const ast::MethodDecl& method) {
  if (ctx) {
    return BuildCapabilitySignature(*ctx, module_path, owner_name, method);
  }
  return BuildCapabilitySignature(module_path, owner_name, method);
}

CapabilitySignature ClassMethodCapabilitySignature(
    const ScopeContext* ctx,
    const ast::ModulePath& module_path,
    const std::string& owner_name,
    const ast::ClassMethodDecl& method) {
  if (ctx) {
    return BuildCapabilitySignature(*ctx, module_path, owner_name, method);
  }
  return BuildCapabilitySignature(module_path, owner_name, method);
}

CapabilitySignature StateMethodCapabilitySignature(
    const ScopeContext* ctx,
    const ast::ModulePath& module_path,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::StateMethodDecl& method) {
  if (ctx) {
    return BuildCapabilitySignature(*ctx, module_path, modal_name, state_name,
                                    method);
  }
  return BuildCapabilitySignature(module_path, modal_name, state_name, method);
}

CapabilitySignature TransitionCapabilitySignature(
    const ScopeContext* ctx,
    const ast::ModulePath& module_path,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::TransitionDecl& transition) {
  if (ctx) {
    return BuildCapabilitySignature(*ctx, module_path, modal_name, state_name,
                                    transition);
  }
  return BuildCapabilitySignature(module_path, modal_name, state_name,
                                  transition);
}

template <typename ResolveCalleeFn>
void AddCallsFromBody(CallGraph& cg,
                      const ProcId& caller,
                      const std::string& caller_module_path,
                      const ast::Block& body,
                      ResolveCalleeFn&& resolve_callee) {
  std::vector<CollectedCallTarget> targets;
  std::vector<CollectedUnresolvedDirectCall> unresolved_calls;
  std::vector<CollectedUnresolvedMethodCall> unresolved_method_calls;
  CollectCallTargetsFromBlock(body, targets, unresolved_calls,
                              unresolved_method_calls);

  for (const auto& target : targets) {
    const std::optional<ProcId> callee =
        resolve_callee(target, caller_module_path, cg);
    if (callee.has_value()) {
      CallGraphEdge edge;
      edge.caller = caller;
      edge.callee = *callee;
      edge.call_span = target.call_span;
      edge.is_dynamic = false;
      cg.AddEdge(std::move(edge));
    } else if (target.callee_expr) {
      cg.AddUnresolvedDirectCall({caller, target.callee_expr, target.call_span});
    }
  }

  for (const auto& unresolved : unresolved_calls) {
    cg.AddUnresolvedDirectCall({caller, unresolved.callee_expr,
                                unresolved.call_span});
  }
  for (const auto& unresolved_method : unresolved_method_calls) {
    UnresolvedMethodCall call;
    call.caller = caller;
    call.receiver_expr = unresolved_method.receiver_expr;
    call.arg_exprs = unresolved_method.arg_exprs;
    call.method_name = unresolved_method.method_name;
    call.call_span = unresolved_method.call_span;
    cg.AddUnresolvedMethodCall(std::move(call));
  }
}

template <typename ResolveCalleeFn>
void AddModuleBodyEdges(CallGraph& cg,
                        const ast::ASTModule& module,
                        ResolveCalleeFn&& resolve_callee) {
  const std::string module_path = ModulePathToString(module.path);

  for (const auto& item : module.items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.body) {
              AddCallsFromBody(cg, {module_path, node.name}, module_path,
                               *node.body, resolve_callee);
            }
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            for (const auto& member : node.members) {
              if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
                AddCallsFromBody(cg,
                                 {module_path,
                                  RecordMethodNodeName(node.name, method->name)},
                                 module_path, *method->body, resolve_callee);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            for (const auto& class_item : node.items) {
              const auto* method =
                  std::get_if<ast::ClassMethodDecl>(&class_item);
              if (!method || !method->body_opt) {
                continue;
              }
              AddCallsFromBody(cg,
                               {module_path,
                                RecordMethodNodeName(node.name, method->name)},
                               module_path, *method->body_opt, resolve_callee);
            }
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            for (const auto& state : node.states) {
              for (const auto& member : state.members) {
                if (const auto* method =
                        std::get_if<ast::StateMethodDecl>(&member)) {
                  AddCallsFromBody(
                      cg,
                      {module_path,
                       StateMethodNodeName(node.name, state.name, method->name)},
                      module_path, *method->body, resolve_callee);
                } else if (const auto* transition =
                               std::get_if<ast::TransitionDecl>(&member)) {
                  AddCallsFromBody(
                      cg,
                      {module_path,
                       TransitionNodeName(node.name, state.name,
                                          transition->name)},
                      module_path, *transition->body, resolve_callee);
                }
              }
            }
          }
        },
        item);
  }
}

template <typename ResolveCalleeFn>
CallGraph BuildCallGraphImpl(const ScopeContext* ctx,
                             const std::vector<const ast::ASTModule*>& modules,
                             ResolveCalleeFn&& resolve_callee) {
  CallGraph cg;

  for (const ast::ASTModule* module : modules) {
    if (!module) {
      continue;
    }

    const std::string module_path = ModulePathToString(module->path);
    for (const auto& item : module->items) {
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
              cg.AddNode(MakeCallableNode(
                  module_path, node.name,
                  ProcCapabilitySignature(ctx, module->path, node), node.span));
            } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
              for (const auto& extern_item : node.items) {
                if (const auto* extern_proc =
                        std::get_if<ast::ExternProcDecl>(&extern_item)) {
                  cg.AddNode(MakeExternNode(module_path, *extern_proc));
                }
              }
            } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
              for (const auto& member : node.members) {
                const auto* method = std::get_if<ast::MethodDecl>(&member);
                if (!method) {
                  continue;
                }
                cg.AddNode(MakeCallableNode(
                    module_path, RecordMethodNodeName(node.name, method->name),
                    MethodCapabilitySignature(ctx, module->path, node.name,
                                              *method),
                    method->span));
              }
            } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
              for (const auto& class_item : node.items) {
                const auto* method =
                    std::get_if<ast::ClassMethodDecl>(&class_item);
                if (!method || !method->body_opt) {
                  continue;
                }
                cg.AddNode(MakeCallableNode(
                    module_path, RecordMethodNodeName(node.name, method->name),
                    ClassMethodCapabilitySignature(ctx, module->path, node.name,
                                                   *method),
                    method->span));
              }
            } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
              for (const auto& state : node.states) {
                for (const auto& member : state.members) {
                  if (const auto* method =
                          std::get_if<ast::StateMethodDecl>(&member)) {
                    cg.AddNode(MakeCallableNode(
                        module_path,
                        StateMethodNodeName(node.name, state.name, method->name),
                        StateMethodCapabilitySignature(ctx, module->path,
                                                       node.name, state.name,
                                                       *method),
                        method->span));
                  } else if (const auto* transition =
                                 std::get_if<ast::TransitionDecl>(&member)) {
                    cg.AddNode(MakeCallableNode(
                        module_path,
                        TransitionNodeName(node.name, state.name,
                                           transition->name),
                        TransitionCapabilitySignature(ctx, module->path,
                                                      node.name, state.name,
                                                      *transition),
                        transition->span));
                  }
                }
              }
            }
          },
          item);
    }
  }

  for (const ast::ASTModule* module : modules) {
    if (!module) {
      continue;
    }
    AddModuleBodyEdges(cg, *module, resolve_callee);
  }

  return cg;
}

}  // namespace

CallGraph BuildCallGraph(const ast::ASTModule& module) {
  SpecDefsCallGraphCaps();
  const std::vector<const ast::ASTModule*> modules = {&module};
  return BuildCallGraphImpl(
      nullptr, modules,
      [](const CollectedCallTarget& target, const std::string& caller_module_path,
         const CallGraph& cg) {
        return ResolveSingleModuleCallee(target, caller_module_path, cg);
      });
}

CallGraph BuildCallGraph(const std::vector<const ast::ASTModule*>& modules) {
  SpecDefsCallGraphCaps();
  return BuildCallGraphImpl(
      nullptr, modules,
      [&modules](const CollectedCallTarget& target,
                 const std::string& caller_module_path, const CallGraph& cg) {
        return ResolveMultiModuleCallee(target, caller_module_path, modules, cg);
      });
}

CallGraph BuildCallGraph(const ScopeContext& ctx, const ast::ASTModule& module) {
  SpecDefsCallGraphCaps();
  const std::vector<const ast::ASTModule*> modules = {&module};
  return BuildCallGraphImpl(
      &ctx, modules,
      [](const CollectedCallTarget& target, const std::string& caller_module_path,
         const CallGraph& cg) {
        return ResolveSingleModuleCallee(target, caller_module_path, cg);
      });
}

CallGraph BuildCallGraph(const ScopeContext& ctx,
                         const std::vector<const ast::ASTModule*>& modules) {
  SpecDefsCallGraphCaps();
  return BuildCallGraphImpl(
      &ctx, modules,
      [&modules](const CollectedCallTarget& target,
                 const std::string& caller_module_path, const CallGraph& cg) {
        return ResolveMultiModuleCallee(target, caller_module_path, modules, cg);
      });
}

// =============================================================================
// Capability flow analysis
// =============================================================================

void AnnotateCapabilityFlow(CallGraph& cg) {
  SpecDefsCallGraphCaps();

  // For each edge, compute capabilities that could flow from caller to callee
  for (auto& edge : cg.GetMutableEdges()) {
    const CallGraphNode* caller = cg.GetNode(edge.caller);
    const CallGraphNode* callee = cg.GetNode(edge.callee);

    if (!caller || !callee) continue;

    // Capabilities that the caller has available
    CapabilitySet caller_caps = caller->cap_sig.provided;

    // Capabilities that the callee requires
    CapabilitySet callee_needs = callee->cap_sig.required;

    // Capabilities passed are the intersection
    edge.capabilities_passed = caller_caps.Intersection(callee_needs);
  }
}

std::vector<CapabilityLeak> DetectCapabilityLeaks(const CallGraph& cg) {
  SpecDefsCallGraphCaps();
  std::vector<CapabilityLeak> leaks;

  // Get all extern nodes (capability sinks)
  auto extern_nodes = cg.GetExternNodes();

  // For each extern node, check if any capability flows to it
  for (const CallGraphNode* extern_node : extern_nodes) {
    auto incoming = cg.GetIncomingEdges(extern_node->id);

    for (const CallGraphEdge* edge : incoming) {
      if (!edge->capabilities_passed.IsEmpty()) {
        // Found a capability leak
        CapabilityLeak leak;
        leak.code = "E-TYP-2623";
        leak.sink = extern_node->id;
        leak.leak_span = edge->call_span;
        leak.path = {edge->caller, edge->callee};

        // Determine which capability leaked
        if (edge->capabilities_passed.has_context) {
          leak.capability = CapabilityKind::Context;
        } else if (edge->capabilities_passed.has_filesystem) {
          leak.capability = CapabilityKind::FileSystem;
        } else if (edge->capabilities_passed.has_network) {
          leak.capability = CapabilityKind::Network;
        } else if (edge->capabilities_passed.has_heap) {
          leak.capability = CapabilityKind::HeapAllocator;
        } else if (edge->capabilities_passed.has_execution_domain) {
          leak.capability = CapabilityKind::ExecutionDomain;
        } else if (edge->capabilities_passed.has_reactor) {
          leak.capability = CapabilityKind::Reactor;
        } else if (edge->capabilities_passed.has_system) {
          leak.capability = CapabilityKind::System;
        }

        std::ostringstream msg;
        msg << "Capability " << CapabilityKindName(leak.capability)
            << " leaked to extern procedure '" << extern_node->id.name << "'";
        leak.message = msg.str();

        leaks.push_back(std::move(leak));
      }
    }
  }

  return leaks;
}

CapabilityChainResult ValidateCapabilityChain(const CallGraph& cg) {
  return ValidateCapabilityChain(cg, nullptr);
}

CapabilityChainResult ValidateCapabilityChain(
    const CallGraph& cg,
    const std::unordered_map<const ast::Expr*, TypeRef>* expr_types) {
  SpecDefsCallGraphCaps();
  CapabilityChainResult result;
  result.valid = true;

  // Get entry point
  const CallGraphNode* entry = cg.GetEntryPoint();
  if (!entry) {
    // No main procedure - may be a library
    return result;
  }

  // Verify entry point receives Context
  if (!entry->cap_sig.required.has_context) {
    result.valid = false;
    result.errors.push_back({"E-CON-0020",
                             "main procedure does not receive Context capability",
                             entry->span});
  }

  // Detect leaks
  result.leaks = DetectCapabilityLeaks(cg);
  if (!result.leaks.empty()) {
    result.valid = false;
  }

  // Validate that all procedures have their capability requirements satisfied
  for (const auto& [id, node] : cg.GetNodes()) {
    if (node.is_extern) continue;

    auto incoming = cg.GetIncomingEdges(id);
    if (incoming.empty() && !IdEq(id.name, "main")) {
      // Unreachable procedure - skip
      continue;
    }

    // For each caller, check that it provides required capabilities
    for (const CallGraphEdge* edge : incoming) {
      const CallGraphNode* caller = cg.GetNode(edge->caller);
      if (!caller) continue;

      SPEC_RULE("NAA-3");
      if (!node.cap_sig.required.IsSubsetOf(caller->cap_sig.provided)) {
        result.valid = false;
        std::ostringstream msg;
        msg << "Procedure '" << id.name << "' requires capabilities "
            << node.cap_sig.required.ToString()
            << " but caller '" << edge->caller.name << "' only provides "
            << caller->cap_sig.provided.ToString();
        result.errors.push_back({"E-CON-0020", msg.str(), edge->call_span});
      }
    }
  }

  if (expr_types) {
    for (const auto& unresolved : cg.GetUnresolvedDirectCalls()) {
      const CallGraphNode* caller = cg.GetNode(unresolved.caller);
      if (!caller || !unresolved.callee_expr) {
        continue;
      }

      const auto type_it = expr_types->find(unresolved.callee_expr);
      if (type_it == expr_types->end()) {
        continue;
      }

      const CapabilitySet callee_required =
          InferCallableParamCapabilities(type_it->second);
      if (callee_required.IsEmpty()) {
        continue;
      }

      SPEC_RULE("NAA-3");
      const CapabilityValidationResult cap_check = ValidateCapabilitySatisfied(
          caller->cap_sig.provided, callee_required, unresolved.call_span);
      if (cap_check.valid) {
        continue;
      }

      result.valid = false;
      std::ostringstream msg;
      msg << "Unresolved direct call in procedure '" << unresolved.caller.name
          << "' requires capabilities " << callee_required.ToString()
          << " inferred from callee expression type, but caller only provides "
          << caller->cap_sig.provided.ToString();
      if (!cap_check.error_message.empty()) {
        msg << "; " << cap_check.error_message;
      }

      const std::string code =
          cap_check.error_code.empty() ? "E-CON-0020" : cap_check.error_code;
      result.errors.push_back({code, msg.str(), unresolved.call_span});
    }

    for (const auto& unresolved_method : cg.GetUnresolvedMethodCalls()) {
      const CallGraphNode* caller = cg.GetNode(unresolved_method.caller);
      if (!caller || !unresolved_method.receiver_expr) {
        continue;
      }

      CapabilitySet method_required = CapabilitySet::Empty();

      const auto recv_it = expr_types->find(unresolved_method.receiver_expr);
      if (recv_it != expr_types->end()) {
        method_required =
            method_required.Union(InferCapabilitiesFromType(recv_it->second));
      }
      for (const ast::Expr* arg_expr : unresolved_method.arg_exprs) {
        if (!arg_expr) {
          continue;
        }
        const auto arg_it = expr_types->find(arg_expr);
        if (arg_it != expr_types->end()) {
          method_required =
              method_required.Union(InferCapabilitiesFromType(arg_it->second));
        }
      }

      if (method_required.IsEmpty()) {
        continue;
      }

      SPEC_RULE("NAA-3");
      const CapabilityValidationResult cap_check = ValidateCapabilitySatisfied(
          caller->cap_sig.provided, method_required, unresolved_method.call_span);
      if (cap_check.valid) {
        continue;
      }

      result.valid = false;
      std::ostringstream msg;
      msg << "Method call '" << unresolved_method.method_name
          << "' in procedure '" << unresolved_method.caller.name
          << "' requires capabilities " << method_required.ToString()
          << " inferred from receiver/argument types, but caller only provides "
          << caller->cap_sig.provided.ToString();
      if (!cap_check.error_message.empty()) {
        msg << "; " << cap_check.error_message;
      }

      const std::string code =
          cap_check.error_code.empty() ? "E-CON-0020" : cap_check.error_code;
      result.errors.push_back({code, msg.str(), unresolved_method.call_span});
    }
  }

  return result;
}

CapabilityChainResult ValidateCapabilityChain(
    const ScopeContext& ctx,
    const CallGraph& cg,
    const std::unordered_map<const ast::Expr*, TypeRef>* expr_types) {
  SpecDefsCallGraphCaps();
  CapabilityChainResult result;
  result.valid = true;

  const CallGraphNode* entry = cg.GetEntryPoint();
  if (!entry) {
    return result;
  }

  if (!entry->cap_sig.required.has_context) {
    result.valid = false;
    result.errors.push_back({"E-CON-0020",
                             "main procedure does not receive Context capability",
                             entry->span});
  }

  result.leaks = DetectCapabilityLeaks(cg);
  if (!result.leaks.empty()) {
    result.valid = false;
  }

  for (const auto& [id, node] : cg.GetNodes()) {
    if (node.is_extern) {
      continue;
    }

    const auto incoming = cg.GetIncomingEdges(id);
    if (incoming.empty() && !IdEq(id.name, "main")) {
      continue;
    }

    for (const CallGraphEdge* edge : incoming) {
      const CallGraphNode* caller = cg.GetNode(edge->caller);
      if (!caller) {
        continue;
      }
      SPEC_RULE("NAA-3");
      if (node.cap_sig.required.IsSubsetOf(caller->cap_sig.provided)) {
        continue;
      }

      result.valid = false;
      std::ostringstream msg;
      msg << "Procedure '" << id.name << "' requires capabilities "
          << node.cap_sig.required.ToString() << " but caller '"
          << edge->caller.name << "' only provides "
          << caller->cap_sig.provided.ToString();
      result.errors.push_back({"E-CON-0020", msg.str(), edge->call_span});
    }
  }

  if (expr_types) {
    for (const auto& unresolved : cg.GetUnresolvedDirectCalls()) {
      const CallGraphNode* caller = cg.GetNode(unresolved.caller);
      if (!caller || !unresolved.callee_expr) {
        continue;
      }

      const auto type_it = expr_types->find(unresolved.callee_expr);
      if (type_it == expr_types->end()) {
        continue;
      }

      const ast::ModulePath caller_module =
          ModulePathFromString(unresolved.caller.module_path);
      const CapabilitySet callee_required = InferCallableParamCapabilities(
          type_it->second, &ctx, &caller_module);
      if (callee_required.IsEmpty()) {
        continue;
      }

      SPEC_RULE("NAA-3");
      const CapabilityValidationResult cap_check = ValidateCapabilitySatisfied(
          caller->cap_sig.provided, callee_required, unresolved.call_span);
      if (cap_check.valid) {
        continue;
      }

      result.valid = false;
      std::ostringstream msg;
      msg << "Unresolved direct call in procedure '" << unresolved.caller.name
          << "' requires capabilities " << callee_required.ToString()
          << " inferred from callee expression type, but caller only provides "
          << caller->cap_sig.provided.ToString();
      if (!cap_check.error_message.empty()) {
        msg << "; " << cap_check.error_message;
      }

      const std::string code =
          cap_check.error_code.empty() ? "E-CON-0020" : cap_check.error_code;
      result.errors.push_back({code, msg.str(), unresolved.call_span});
    }

    for (const auto& unresolved_method : cg.GetUnresolvedMethodCalls()) {
      const CallGraphNode* caller = cg.GetNode(unresolved_method.caller);
      if (!caller || !unresolved_method.receiver_expr) {
        continue;
      }

      CapabilitySet method_required = CapabilitySet::Empty();
      const ast::ModulePath caller_module =
          ModulePathFromString(unresolved_method.caller.module_path);

      const auto recv_it = expr_types->find(unresolved_method.receiver_expr);
      if (recv_it != expr_types->end()) {
        method_required = method_required.Union(
            InferCapabilitiesFromType(ctx, caller_module, recv_it->second));
      }
      for (const ast::Expr* arg_expr : unresolved_method.arg_exprs) {
        if (!arg_expr) {
          continue;
        }
        const auto arg_it = expr_types->find(arg_expr);
        if (arg_it != expr_types->end()) {
          method_required = method_required.Union(
              InferCapabilitiesFromType(ctx, caller_module, arg_it->second));
        }
      }

      if (method_required.IsEmpty()) {
        continue;
      }

      SPEC_RULE("NAA-3");
      const CapabilityValidationResult cap_check = ValidateCapabilitySatisfied(
          caller->cap_sig.provided, method_required, unresolved_method.call_span);
      if (cap_check.valid) {
        continue;
      }

      result.valid = false;
      std::ostringstream msg;
      msg << "Method call '" << unresolved_method.method_name
          << "' in procedure '" << unresolved_method.caller.name
          << "' requires capabilities " << method_required.ToString()
          << " inferred from receiver/argument types, but caller only provides "
          << caller->cap_sig.provided.ToString();
      if (!cap_check.error_message.empty()) {
        msg << "; " << cap_check.error_message;
      }

      const std::string code =
          cap_check.error_code.empty() ? "E-CON-0020" : cap_check.error_code;
      result.errors.push_back({code, msg.str(), unresolved_method.call_span});
    }
  }

  return result;
}

// =============================================================================
// Reachability analysis
// =============================================================================

bool ReachabilityMatrix::CanReach(const ProcId& from, const ProcId& to) const {
  auto it = reachable_.find(from);
  if (it == reachable_.end()) {
    return false;
  }
  return it->second.find(to) != it->second.end();
}

std::unordered_set<ProcId, ProcIdHash> ReachabilityMatrix::ReachableFrom(
    const ProcId& from) const {
  auto it = reachable_.find(from);
  if (it == reachable_.end()) {
    return {};
  }
  return it->second;
}

std::unordered_set<ProcId, ProcIdHash> ReachabilityMatrix::ReachersOf(
    const ProcId& to) const {
  auto it = reachers_.find(to);
  if (it == reachers_.end()) {
    return {};
  }
  return it->second;
}

ReachabilityMatrix ComputeTransitiveClosure(const CallGraph& cg) {
  SpecDefsCallGraphCaps();
  ReachabilityMatrix matrix;

  // Initialize with direct edges
  for (const auto& edge : cg.GetEdges()) {
    matrix.reachable_[edge.caller].insert(edge.callee);
    matrix.reachers_[edge.callee].insert(edge.caller);
  }

  // Fixed-point iteration (Floyd-Warshall style)
  bool changed = true;
  while (changed) {
    changed = false;

    for (const auto& [from, reachable] : matrix.reachable_) {
      std::unordered_set<ProcId, ProcIdHash> new_reachable;

      for (const auto& mid : reachable) {
        auto it = matrix.reachable_.find(mid);
        if (it != matrix.reachable_.end()) {
          for (const auto& to : it->second) {
            if (reachable.find(to) == reachable.end()) {
              new_reachable.insert(to);
            }
          }
        }
      }

      for (const auto& to : new_reachable) {
        matrix.reachable_[from].insert(to);
        matrix.reachers_[to].insert(from);
        changed = true;
      }
    }
  }

  return matrix;
}

// =============================================================================
// Capability propagation
// =============================================================================

void PropagateCapabilityRequirements(CallGraph& cg) {
  SpecDefsCallGraphCaps();

  // Capability signatures are procedure-contract-based and must remain stable.
  // This pass only refreshes per-edge flow annotations.
  AnnotateCapabilityFlow(cg);
}

}  // namespace cursive::analysis

