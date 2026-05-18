// =============================================================================
// cap_requirements.cpp - Capability Requirements Inference and Validation
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 5.9.3 "Capability Requirements" (lines 13210-13300)
//   - Section 5.9.5 "Capability Classes" (lines 13400-13600)
//   - Section 8.10 "E-CAP Errors" (lines 21900-22000)
//
// SOURCE FILES:
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_io.cpp
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_heap.cpp
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_concurrency.cpp
//
// FUNCTIONS IMPLEMENTED:
//   - InferCapabilityRequirements() - Infer capabilities from procedure
//   - ValidateCapabilitySatisfied() - Verify caller provides capabilities
//   - BuildCapabilitySignature() - Build capability signature for procedure
//   - CheckCapabilitySubset() - Verify provided capabilities satisfy required
//   - PropagateCapabilityRequirements() - Propagate through call graph
//
// =============================================================================

#include "04_analysis/caps/cap_requirements.h"

#include <sstream>
#include <unordered_set>
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_io.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_network.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/caps/cap_time.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_decls.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsCapRequirements() {
  SPEC_DEF("CapabilityRequirements", "5.9.3");
  SPEC_DEF("CapabilityClasses", "5.9.5");
  SPEC_DEF("CapabilitySatisfaction", "5.9.3");
  SPEC_DEF("CapabilityPropagation", "5.9.3");
}

ast::ClassPath AsClassPath(const TypePath& path) {
  ast::ClassPath out;
  out.reserve(path.size());
  for (const auto& seg : path) {
    out.push_back(seg);
  }
  return out;
}

ast::Path AsAstPath(const TypePath& path) {
  ast::Path out;
  out.reserve(path.size());
  for (const auto& seg : path) {
    out.push_back(seg);
  }
  return out;
}

std::string JoinPath(const ast::Path& path) {
  std::string out;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      out += "::";
    }
    out += path[i];
  }
  return out;
}

ast::ModulePath ModulePathOf(const ast::Path& path) {
  ast::ModulePath module_path;
  if (path.empty()) {
    return module_path;
  }
  module_path.assign(path.begin(), path.end() - 1);
  return module_path;
}

const TypeDecl* LookupNominalTypeDecl(const ScopeContext& ctx,
                                      const ast::ModulePath& current_module,
                                      const TypePath& path,
                                      ast::Path* resolved_path) {
  if (path.empty()) {
    return nullptr;
  }

  const auto exact = AsAstPath(path);
  auto exact_it = ctx.sigma.types.find(PathKeyOf(exact));
  if (exact_it != ctx.sigma.types.end()) {
    if (resolved_path) {
      *resolved_path = exact;
    }
    return &exact_it->second;
  }

  ast::Path current_qualified = current_module;
  current_qualified.insert(current_qualified.end(), path.begin(), path.end());
  auto current_it = ctx.sigma.types.find(PathKeyOf(current_qualified));
  if (current_it != ctx.sigma.types.end()) {
    if (resolved_path) {
      *resolved_path = current_qualified;
    }
    return &current_it->second;
  }

  if (path.size() == 1 && !current_module.empty()) {
    ast::Path root_qualified;
    root_qualified.push_back(current_module.front());
    root_qualified.push_back(path.front());
    auto root_it = ctx.sigma.types.find(PathKeyOf(root_qualified));
    if (root_it != ctx.sigma.types.end()) {
      if (resolved_path) {
        *resolved_path = root_qualified;
      }
      return &root_it->second;
    }
  }

  std::optional<ast::Path> unique_match;
  for (const auto& [key, _decl] : ctx.sigma.types) {
    if (key.empty()) {
      continue;
    }
    if (!IdEq(key.back(), path.back())) {
      continue;
    }
    if (key.size() < path.size()) {
      continue;
    }

    bool suffix_match = true;
    for (std::size_t i = 0; i < path.size(); ++i) {
      const auto key_index = key.size() - path.size() + i;
      if (!IdEq(key[key_index], path[i])) {
        suffix_match = false;
        break;
      }
    }
    if (!suffix_match) {
      continue;
    }

    ast::Path candidate(key.begin(), key.end());
    if (unique_match.has_value()) {
      // Ambiguous nominal path: do not guess.
      return nullptr;
    }
    unique_match = std::move(candidate);
  }

  if (!unique_match.has_value()) {
    return nullptr;
  }
  auto match_it = ctx.sigma.types.find(PathKeyOf(*unique_match));
  if (match_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  if (resolved_path) {
    *resolved_path = *unique_match;
  }
  return &match_it->second;
}

TypePath NominalTypePath(const ast::ModulePath& current_module,
                         std::string_view type_name) {
  TypePath path = current_module;
  path.emplace_back(type_name);
  return path;
}

CapabilitySignature CapabilitySignatureFromBindings(
    const std::vector<std::pair<std::string, TypeRef>>& bindings) {
  CapabilitySignature sig{};

  for (std::size_t i = 0; i < bindings.size(); ++i) {
    const TypeRef& binding_type = bindings[i].second;
    if (!binding_type) {
      continue;
    }

    const CapabilitySet caps = InferCapabilitiesFromType(binding_type);
    if (caps.IsEmpty()) {
      continue;
    }

    sig.required = sig.required.Union(caps);
    sig.capability_params.push_back(i);
  }

  sig.provided = sig.required;
  return sig;
}

CapabilitySignature CapabilitySignatureFromBindings(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::vector<std::pair<std::string, TypeRef>>& bindings) {
  CapabilitySignature sig{};

  for (std::size_t i = 0; i < bindings.size(); ++i) {
    const TypeRef& binding_type = bindings[i].second;
    if (!binding_type) {
      continue;
    }

    const CapabilitySet caps =
        InferCapabilitiesFromType(ctx, current_module, binding_type);
    if (caps.IsEmpty()) {
      continue;
    }

    sig.required = sig.required.Union(caps);
    sig.capability_params.push_back(i);
  }

  sig.provided = sig.required;
  return sig;
}

CapabilitySignature CapabilitySignatureFromReceiverAndParams(
    const TypeRef& self_type,
    const ast::Receiver& receiver,
    const std::vector<ast::Param>& params) {
  CapabilitySignature sig{};

  auto add_caps = [&](const CapabilitySet& caps, std::size_t index) {
    if (caps.IsEmpty()) {
      return;
    }
    sig.required = sig.required.Union(caps);
    sig.capability_params.push_back(index);
  };

  std::size_t capability_index = 0;
  if (std::holds_alternative<ast::ReceiverShorthand>(receiver)) {
    add_caps(InferCapabilitiesFromType(self_type), capability_index++);
  } else if (const auto* explicit_recv =
                 std::get_if<ast::ReceiverExplicit>(&receiver)) {
    if (explicit_recv->type) {
      add_caps(InferCapabilitiesFromAstType(*explicit_recv->type),
               capability_index++);
    }
  }

  for (const auto& param : params) {
    if (param.type) {
      add_caps(InferCapabilitiesFromAstType(*param.type), capability_index);
    }
    ++capability_index;
  }

  sig.provided = sig.required;
  return sig;
}

CapabilitySignature BuildMethodCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const TypeRef& self_type,
    const ast::Receiver& receiver,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Type>& return_type_opt) {
  const SignatureResult sig_result =
      BuildMethodSignature(ctx, self_type, receiver, params, return_type_opt);
  if (sig_result.ok) {
    return CapabilitySignatureFromBindings(ctx, current_module,
                                           sig_result.bindings);
  }

  return CapabilitySignatureFromReceiverAndParams(self_type, receiver, params);
}

CapabilitySet BlockUsesCapabilities(const ast::Block& block);

CapabilitySet StmtUsesCapabilities(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) -> CapabilitySet {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          if (node.binding.init) {
            return ExpressionUsesCapabilities(*node.binding.init);
          }
        } else if constexpr (std::is_same_v<T, ast::ExprStmt> ||
                             std::is_same_v<T, ast::AssignStmt>) {
          if (node.value) {
            return ExpressionUsesCapabilities(*node.value);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          if (node.value_opt) {
            return ExpressionUsesCapabilities(*node.value_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            return BlockUsesCapabilities(*node.body);
          }
        }

        return CapabilitySet::Empty();
      },
      stmt);
}

CapabilitySet BlockUsesCapabilities(const ast::Block& block) {
  CapabilitySet caps = CapabilitySet::Empty();
  for (const auto& stmt : block.stmts) {
    caps = caps.Union(StmtUsesCapabilities(stmt));
  }
  if (block.tail_opt) {
    caps = caps.Union(ExpressionUsesCapabilities(*block.tail_opt));
  }
  return caps;
}

CapabilitySet InferCapabilitiesFromAstTypeInternal(
    const ast::Type& type,
    const ScopeContext* ctx,
    const ast::ModulePath* current_module,
    std::unordered_set<std::string>& visiting);

CapabilitySet ExpandNominalDeclCaps(const ScopeContext* ctx,
                                    const ast::ModulePath* current_module,
                                    const TypePath& path,
                                    std::unordered_set<std::string>& visiting) {
  if (!ctx || !current_module || path.empty()) {
    return CapabilitySet::Empty();
  }

  ast::Path resolved;
  const TypeDecl* decl =
      LookupNominalTypeDecl(*ctx, *current_module, path, &resolved);
  if (!decl) {
    return CapabilitySet::Empty();
  }

  const std::string visit_key = JoinPath(resolved);
  if (visiting.find(visit_key) != visiting.end()) {
    return CapabilitySet::Empty();
  }
  visiting.insert(visit_key);

  CapabilitySet out = CapabilitySet::Empty();
  const auto decl_module = ModulePathOf(resolved);
  std::visit(
      [&](const auto& node) {
        using D = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<D, ast::RecordDecl>) {
          for (const auto& member : node.members) {
            const auto* field = std::get_if<ast::FieldDecl>(&member);
            if (!field || !field->type) {
              continue;
            }
            out = out.Union(InferCapabilitiesFromAstTypeInternal(
                *field->type, ctx, &decl_module, visiting));
          }
        } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
          for (const auto& variant : node.variants) {
            if (!variant.payload_opt.has_value()) {
              continue;
            }
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                    for (const auto& elem : payload.elements) {
                      out = out.Union(InferCapabilitiesFromAstTypeInternal(
                          *elem, ctx, &decl_module, visiting));
                    }
                  } else if constexpr (std::is_same_v<
                                           P, ast::VariantPayloadRecord>) {
                    for (const auto& field : payload.fields) {
                      out = out.Union(InferCapabilitiesFromAstTypeInternal(
                          *field.type, ctx, &decl_module, visiting));
                    }
                  }
                },
                *variant.payload_opt);
          }
        } else if constexpr (std::is_same_v<D, ast::ModalDecl>) {
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              if (const auto* field = std::get_if<ast::StateFieldDecl>(&member);
                  field && field->type) {
                out = out.Union(InferCapabilitiesFromAstTypeInternal(
                    *field->type, ctx, &decl_module, visiting));
              }
            }
          }
        } else if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
          if (node.type) {
            out = out.Union(InferCapabilitiesFromAstTypeInternal(
                *node.type, ctx, &decl_module, visiting));
          }
        }
      },
      *decl);

  visiting.erase(visit_key);
  return out;
}

CapabilitySet InferCapabilitiesFromTypeInternal(
    const TypeRef& type,
    const ScopeContext* ctx,
    const ast::ModulePath* current_module,
    std::unordered_set<std::string>& visiting) {
  if (!type) {
    return CapabilitySet::Empty();
  }

  return std::visit(
      [&](const auto& node) -> CapabilitySet {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypeDynamic>) {
          auto kind = CapabilityKindFromDynamic(node);
          if (!kind.has_value()) {
            return CapabilitySet::Empty();
          }
          CapabilitySet out{};
          out.Add(*kind);
          return out;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          CapabilitySet out = CapabilitySet::Empty();
          if (auto kind = CapabilityKindFromPath(node.path); kind.has_value()) {
            out.Add(*kind);
          }
          for (const auto& arg : node.generic_args) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                arg, ctx, current_module, visiting));
          }
          out = out.Union(
              ExpandNominalDeclCaps(ctx, current_module, node.path, visiting));
          return out;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& arg : node.generic_args) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                arg, ctx, current_module, visiting));
          }
          out = out.Union(
              ExpandNominalDeclCaps(ctx, current_module, node.path, visiting));
          return out;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& member : node.members) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                member, ctx, current_module, visiting));
          }
          return out;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& elem : node.elements) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                elem, ctx, current_module, visiting));
          }
          return out;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return InferCapabilitiesFromTypeInternal(
              node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return InferCapabilitiesFromTypeInternal(
              node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return InferCapabilitiesFromTypeInternal(
              node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return InferCapabilitiesFromTypeInternal(
              node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& param : node.params) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                param.type, ctx, current_module, visiting));
          }
          out = out.Union(InferCapabilitiesFromTypeInternal(
              node.ret, ctx, current_module, visiting));
          return out;
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& param : node.params) {
            out = out.Union(InferCapabilitiesFromTypeInternal(
                param.second, ctx, current_module, visiting));
          }
          out = out.Union(InferCapabilitiesFromTypeInternal(
              node.ret, ctx, current_module, visiting));
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              out = out.Union(InferCapabilitiesFromTypeInternal(
                  dep.type, ctx, current_module, visiting));
            }
          }
          return out;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          return InferCapabilitiesFromTypeInternal(
              node.base, ctx, current_module, visiting);
        } else {
          return CapabilitySet::Empty();
        }
      },
      type->node);
}

CapabilitySet InferCapabilitiesFromAstTypeInternal(
    const ast::Type& type,
    const ScopeContext* ctx,
    const ast::ModulePath* current_module,
    std::unordered_set<std::string>& visiting) {
  return std::visit(
      [&](const auto& node) -> CapabilitySet {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          auto kind = CapabilityKindFromPath(node.path);
          if (!kind.has_value()) {
            return CapabilitySet::Empty();
          }
          CapabilitySet out{};
          out.Add(*kind);
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          CapabilitySet out = CapabilitySet::Empty();
          if (auto kind = CapabilityKindFromPath(node.path); kind.has_value()) {
            out.Add(*kind);
          }
          for (const auto& arg : node.generic_args) {
            if (arg) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*arg, ctx, current_module, visiting));
            }
          }
          out = out.Union(
              ExpandNominalDeclCaps(ctx, current_module, node.path, visiting));
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& arg : node.generic_args) {
            if (arg) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*arg, ctx, current_module, visiting));
            }
          }
          out = out.Union(
              ExpandNominalDeclCaps(ctx, current_module, node.path, visiting));
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          if (!node.base) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          if (!node.base) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.base, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& member : node.types) {
            if (member) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*member, ctx, current_module, visiting));
            }
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& elem : node.elements) {
            if (elem) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*elem, ctx, current_module, visiting));
            }
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          if (!node.element) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          if (!node.element) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          if (!node.element) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          if (!node.element) {
            return CapabilitySet::Empty();
          }
          return InferCapabilitiesFromAstTypeInternal(
              *node.element, ctx, current_module, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& param : node.params) {
            if (param.type) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*param.type, ctx, current_module, visiting));
            }
          }
          if (node.ret) {
            out = out.Union(
                InferCapabilitiesFromAstTypeInternal(*node.ret, ctx, current_module, visiting));
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          CapabilitySet out = CapabilitySet::Empty();
          for (const auto& param : node.params) {
            if (param.type) {
              out = out.Union(
                  InferCapabilitiesFromAstTypeInternal(*param.type, ctx, current_module, visiting));
            }
          }
          if (node.ret) {
            out = out.Union(
                InferCapabilitiesFromAstTypeInternal(*node.ret, ctx, current_module, visiting));
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (dep.type) {
                out = out.Union(InferCapabilitiesFromAstTypeInternal(
                    *dep.type, ctx, current_module, visiting));
              }
            }
          }
          return out;
        } else {
          return CapabilitySet::Empty();
        }
      },
      type.node);
}

}  // namespace

// =============================================================================
// Capability kind utilities
// =============================================================================

std::string_view CapabilityKindName(CapabilityKind kind) {
  SpecDefsCapRequirements();
  switch (kind) {
    case CapabilityKind::IO:
      return "IO";
    case CapabilityKind::Network:
      return "Network";
    case CapabilityKind::HeapAllocator:
      return "HeapAllocator";
    case CapabilityKind::ExecutionDomain:
      return "ExecutionDomain";
    case CapabilityKind::Reactor:
      return "Reactor";
    case CapabilityKind::System:
      return "System";
    case CapabilityKind::Time:
      return "Time";
    case CapabilityKind::Context:
      return "Context";
  }
  return "Unknown";
}

std::optional<CapabilityKind> CapabilityKindFromPath(const TypePath& path) {
  SpecDefsCapRequirements();
  const auto class_path = AsClassPath(path);
  if (IsIOClassPath(class_path)) {
    return CapabilityKind::IO;
  }
  if (IsNetworkClassPath(class_path)) {
    return CapabilityKind::Network;
  }
  if (IsHeapAllocatorClassPath(class_path)) {
    return CapabilityKind::HeapAllocator;
  }
  if (IsExecutionDomainTypePath(path)) {
    return CapabilityKind::ExecutionDomain;
  }
  if (IsReactorClassPath(class_path)) {
    return CapabilityKind::Reactor;
  }
  if (IsSystemTypePath(path)) {
    return CapabilityKind::System;
  }
  if (IsTimeClassPath(class_path) || IsMonotonicTimeClassPath(class_path) ||
      IsWallTimeClassPath(class_path)) {
    return CapabilityKind::Time;
  }
  if (IsContextTypePath(path)) {
    return CapabilityKind::Context;
  }
  return std::nullopt;
}

std::optional<CapabilityKind> CapabilityKindFromDynamic(
    const TypeDynamic& dyn) {
  return CapabilityKindFromPath(dyn.path);
}

// =============================================================================
// CapabilitySet implementation
// =============================================================================

CapabilitySet CapabilitySet::Empty() {
  return CapabilitySet{};
}

CapabilitySet CapabilitySet::FromContext() {
  CapabilitySet set{};
  set.has_context = true;
  set.has_io = true;
  set.has_network = true;
  set.has_heap = true;
  set.has_execution_domain = true;
  set.has_reactor = true;
  set.has_system = true;
  set.has_time = true;
  return set;
}

void CapabilitySet::Add(CapabilityKind kind) {
  switch (kind) {
    case CapabilityKind::IO:
      has_io = true;
      break;
    case CapabilityKind::Network:
      has_network = true;
      break;
    case CapabilityKind::HeapAllocator:
      has_heap = true;
      break;
    case CapabilityKind::ExecutionDomain:
      has_execution_domain = true;
      break;
    case CapabilityKind::Reactor:
      has_reactor = true;
      break;
    case CapabilityKind::System:
      has_system = true;
      break;
    case CapabilityKind::Time:
      has_time = true;
      break;
    case CapabilityKind::Context:
      has_context = true;
      has_io = true;
      has_network = true;
      has_heap = true;
      has_execution_domain = true;
      has_reactor = true;
      has_system = true;
      has_time = true;
      break;
  }
}

bool CapabilitySet::Has(CapabilityKind kind) const {
  // Context implies all capabilities
  if (has_context) {
    return true;
  }
  switch (kind) {
    case CapabilityKind::IO:
      return has_io;
    case CapabilityKind::Network:
      return has_network;
    case CapabilityKind::HeapAllocator:
      return has_heap;
    case CapabilityKind::ExecutionDomain:
      return has_execution_domain;
    case CapabilityKind::Reactor:
      return has_reactor;
    case CapabilityKind::System:
      return has_system;
    case CapabilityKind::Time:
      return has_time;
    case CapabilityKind::Context:
      return has_context;
  }
  return false;
}

bool CapabilitySet::IsSubsetOf(const CapabilitySet& other) const {
  // If other has Context, it satisfies everything
  if (other.has_context) {
    return true;
  }
  // Check individual capabilities
  if (has_io && !other.has_io) return false;
  if (has_network && !other.has_network) return false;
  if (has_heap && !other.has_heap) return false;
  if (has_execution_domain && !other.has_execution_domain) return false;
  if (has_reactor && !other.has_reactor) return false;
  if (has_system && !other.has_system) return false;
  if (has_time && !other.has_time) return false;
  if (has_context && !other.has_context) return false;
  return true;
}

CapabilitySet CapabilitySet::Union(const CapabilitySet& other) const {
  CapabilitySet result{};
  result.has_io = has_io || other.has_io;
  result.has_network = has_network || other.has_network;
  result.has_heap = has_heap || other.has_heap;
  result.has_execution_domain = has_execution_domain || other.has_execution_domain;
  result.has_reactor = has_reactor || other.has_reactor;
  result.has_system = has_system || other.has_system;
  result.has_time = has_time || other.has_time;
  result.has_context = has_context || other.has_context;
  return result;
}

CapabilitySet CapabilitySet::Intersection(const CapabilitySet& other) const {
  CapabilitySet result{};
  result.has_io = has_io && other.has_io;
  result.has_network = has_network && other.has_network;
  result.has_heap = has_heap && other.has_heap;
  result.has_execution_domain = has_execution_domain && other.has_execution_domain;
  result.has_reactor = has_reactor && other.has_reactor;
  result.has_system = has_system && other.has_system;
  result.has_time = has_time && other.has_time;
  result.has_context = has_context && other.has_context;
  return result;
}

bool CapabilitySet::IsEmpty() const {
  return !has_io && !has_network && !has_heap &&
         !has_execution_domain &&
         !has_reactor && !has_system && !has_time && !has_context;
}

std::string CapabilitySet::ToString() const {
  std::ostringstream oss;
  oss << "{";
  bool first = true;
  auto append = [&](std::string_view name) {
    if (!first) oss << ", ";
    oss << name;
    first = false;
  };
  if (has_context) {
    append("Context");
  } else {
    if (has_io) append("IO");
    if (has_network) append("Network");
    if (has_heap) append("HeapAllocator");
    if (has_execution_domain) append("ExecutionDomain");
    if (has_reactor) append("Reactor");
    if (has_system) append("System");
    if (has_time) append("Time");
  }
  oss << "}";
  return oss.str();
}

// =============================================================================
// Capability inference from types
// =============================================================================

CapabilitySet InferCapabilitiesFromType(const TypeRef& type) {
  SpecDefsCapRequirements();
  std::unordered_set<std::string> visiting;
  return InferCapabilitiesFromTypeInternal(type, nullptr, nullptr, visiting);
}

CapabilitySet InferCapabilitiesFromType(const ScopeContext& ctx,
                                        const ast::ModulePath& current_module,
                                        const TypeRef& type) {
  SpecDefsCapRequirements();
  std::unordered_set<std::string> visiting;
  return InferCapabilitiesFromTypeInternal(type, &ctx, &current_module,
                                           visiting);
}

CapabilitySet InferCapabilitiesFromAstType(const ast::Type& type) {
  SpecDefsCapRequirements();
  std::unordered_set<std::string> visiting;
  return InferCapabilitiesFromAstTypeInternal(type, nullptr, nullptr, visiting);
}

CapabilitySet InferCapabilitiesFromAstType(const ScopeContext& ctx,
                                           const ast::ModulePath& current_module,
                                           const ast::Type& type) {
  SpecDefsCapRequirements();
  std::unordered_set<std::string> visiting;
  return InferCapabilitiesFromAstTypeInternal(type, &ctx, &current_module,
                                              visiting);
}

CapabilitySet InferCapabilitiesFromParams(
    const std::vector<ast::Param>& params) {
  SpecDefsCapRequirements();
  CapabilitySet result{};

  for (const auto& param : params) {
    if (param.type) {
      result = result.Union(InferCapabilitiesFromAstType(*param.type));
    }
  }

  return result;
}

CapabilitySet InferCapabilitiesFromParams(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::vector<ast::Param>& params) {
  SpecDefsCapRequirements();
  CapabilitySet result = CapabilitySet::Empty();
  for (const auto& param : params) {
    if (param.type) {
      result = result.Union(
          InferCapabilitiesFromAstType(ctx, current_module, *param.type));
    }
  }
  return result;
}

// =============================================================================
// Capability signature computation
// =============================================================================

CapabilitySignature BuildCapabilitySignature(const ast::ProcedureDecl& proc) {
  SpecDefsCapRequirements();
  CapabilitySignature sig{};

  // Infer capabilities from parameter types
  for (std::size_t i = 0; i < proc.params.size(); ++i) {
    const auto& param = proc.params[i];
    if (param.type) {
      auto caps = InferCapabilitiesFromAstType(*param.type);
      if (!caps.IsEmpty()) {
        sig.required = sig.required.Union(caps);
        sig.capability_params.push_back(i);
      }
    }
  }

  // Capabilities available to callees are those we receive
  sig.provided = sig.required;

  return sig;
}

CapabilitySignature BuildCapabilitySignature(const ScopeContext& ctx,
                                            const ast::ModulePath& current_module,
                                            const ast::ProcedureDecl& proc) {
  SpecDefsCapRequirements();
  CapabilitySignature sig{};

  for (std::size_t i = 0; i < proc.params.size(); ++i) {
    const auto& param = proc.params[i];
    if (!param.type) {
      continue;
    }
    const auto caps =
        InferCapabilitiesFromAstType(ctx, current_module, *param.type);
    if (caps.IsEmpty()) {
      continue;
    }
    sig.required = sig.required.Union(caps);
    sig.capability_params.push_back(i);
  }

  sig.provided = sig.required;
  return sig;
}

CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& record_name,
    const ast::MethodDecl& method) {
  const TypeRef self_type =
      MakeTypePath(NominalTypePath(current_module, record_name));
  return CapabilitySignatureFromReceiverAndParams(self_type, method.receiver,
                                                  method.params);
}

CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& record_name,
    const ast::MethodDecl& method) {
  const TypeRef self_type =
      MakeTypePath(NominalTypePath(current_module, record_name));
  return BuildMethodCapabilitySignature(ctx, current_module, self_type,
                                        method.receiver, method.params,
                                        method.return_type_opt);
}

CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& class_name,
    const ast::ClassMethodDecl& method) {
  const TypeRef self_type =
      MakeTypePath(NominalTypePath(current_module, class_name));
  return CapabilitySignatureFromReceiverAndParams(self_type, method.receiver,
                                                  method.params);
}

CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& class_name,
    const ast::ClassMethodDecl& method) {
  const TypeRef self_type =
      MakeTypePath(NominalTypePath(current_module, class_name));
  return BuildMethodCapabilitySignature(ctx, current_module, self_type,
                                        method.receiver, method.params,
                                        method.return_type_opt);
}

CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::StateMethodDecl& method) {
  const TypeRef self_type = MakeTypeModalState(
      NominalTypePath(current_module, modal_name), state_name, {});
  return CapabilitySignatureFromReceiverAndParams(self_type, method.receiver,
                                                  method.params);
}

CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::StateMethodDecl& method) {
  const TypeRef self_type = MakeTypeModalState(
      NominalTypePath(current_module, modal_name), state_name, {});
  return BuildMethodCapabilitySignature(ctx, current_module, self_type,
                                        method.receiver, method.params,
                                        method.return_type_opt);
}

CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::TransitionDecl& transition) {
  const TypeRef self_type = MakeTypeModalState(
      NominalTypePath(current_module, modal_name), state_name, {});
  const ast::Receiver transition_receiver =
      ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
  return CapabilitySignatureFromReceiverAndParams(self_type, transition_receiver,
                                                  transition.params);
}

CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::TransitionDecl& transition) {
  const TypeRef self_type = MakeTypeModalState(
      NominalTypePath(current_module, modal_name), state_name, {});
  const ast::Receiver transition_receiver =
      ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
  return BuildMethodCapabilitySignature(ctx, current_module, self_type,
                                        transition_receiver, transition.params,
                                        nullptr);
}

// =============================================================================
// Capability validation
// =============================================================================

CapabilityValidationResult ValidateCapabilitySatisfied(
    const CapabilitySet& provided,
    const CapabilitySet& required,
    const core::Span& call_span) {
  SpecDefsCapRequirements();
  CapabilityValidationResult result{};

  if (required.IsSubsetOf(provided)) {
    result.valid = true;
    return result;
  }

  // Compute missing capabilities
  CapabilitySet missing{};
  if (required.has_io && !provided.has_io) {
    missing.has_io = true;
  }
  if (required.has_network && !provided.has_network) {
    missing.has_network = true;
  }
  if (required.has_heap && !provided.has_heap) {
    missing.has_heap = true;
  }
  if (required.has_execution_domain && !provided.has_execution_domain) {
    missing.has_execution_domain = true;
  }
  if (required.has_reactor && !provided.has_reactor) {
    missing.has_reactor = true;
  }
  if (required.has_system && !provided.has_system) {
    missing.has_system = true;
  }
  if (required.has_time && !provided.has_time) {
    missing.has_time = true;
  }
  if (required.has_context && !provided.has_context) {
    missing.has_context = true;
  }

  result.valid = false;
  result.error_code = "E-CON-0020";
  result.error_message =
      "Missing required capability: " + missing.ToString() +
      "; provided: " + provided.ToString();
  result.missing = missing;

  return result;
}

CapabilityValidationResult ValidateProcedureCapabilities(
    const ast::ProcedureDecl& proc,
    const CapabilitySet& available) {
  SpecDefsCapRequirements();
  auto sig = BuildCapabilitySignature(proc);
  return ValidateCapabilitySatisfied(available, sig.required, proc.span);
}

// =============================================================================
// Capability checking for expressions
// =============================================================================

std::optional<CapabilityKind> MethodCallRequiresCapability(
    const TypeRef& receiver_type,
    std::string_view method_name) {
  SpecDefsCapRequirements();
  if (!receiver_type) {
    return std::nullopt;
  }

  // Check if receiver is a capability type
  if (const auto* dyn = std::get_if<TypeDynamic>(&receiver_type->node)) {
    const auto class_path = AsClassPath(dyn->path);

    // IO methods
    if (IsIOClassPath(class_path)) {
      if (LookupIOMethodSig(method_name)) {
        return CapabilityKind::IO;
      }
    }

    // Network methods
    if (IsNetworkClassPath(class_path)) {
      if (LookupNetworkMethodSig(method_name)) {
        return CapabilityKind::Network;
      }
    }

    // HeapAllocator methods
    if (IsHeapAllocatorClassPath(class_path)) {
      if (LookupHeapAllocatorMethodSig(method_name)) {
        return CapabilityKind::HeapAllocator;
      }
    }

    if (IsTimeClassPath(class_path)) {
      if (LookupTimeMethodSig(method_name)) {
        return CapabilityKind::Time;
      }
    }
    if (IsMonotonicTimeClassPath(class_path)) {
      if (LookupMonotonicTimeMethodSig(method_name)) {
        return CapabilityKind::Time;
      }
    }
    if (IsWallTimeClassPath(class_path)) {
      if (LookupWallTimeMethodSig(method_name)) {
        return CapabilityKind::Time;
      }
    }

    // ExecutionDomain methods
    if (IsExecutionDomainTypePath(dyn->path)) {
      if (LookupExecutionDomainMethodSig(method_name)) {
        return CapabilityKind::ExecutionDomain;
      }
    }
  }

  // Check for Context methods
  if (const auto* path = std::get_if<TypePathType>(&receiver_type->node)) {
    if (IsContextTypePath(path->path)) {
      if (LookupContextMethodSig(method_name)) {
        return CapabilityKind::Context;
      }
    }
    if (IsSystemTypePath(path->path)) {
      if (LookupSystemMethodSig(method_name)) {
        return CapabilityKind::System;
      }
    }
  }

  return std::nullopt;
}

CapabilitySet ExpressionCapabilityUsage(const ast::Expr& expr) {
  SpecDefsCapRequirements();
  return std::visit(
      [](const auto& node) -> CapabilitySet {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.callee) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.callee));
          }
          for (const auto& arg : node.args) {
            if (arg.value) {
              caps = caps.Union(ExpressionCapabilityUsage(*arg.value));
            }
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          std::visit(
              [&](const auto& apply_args) {
                using A = std::decay_t<decltype(apply_args)>;
                if constexpr (std::is_same_v<A, ast::ParenArgs>) {
                  for (const auto& arg : apply_args.args) {
                    if (arg.value) {
                      caps = caps.Union(
                          ExpressionCapabilityUsage(*arg.value));
                    }
                  }
                } else if constexpr (std::is_same_v<A, ast::BraceArgs>) {
                  for (const auto& field : apply_args.fields) {
                    if (field.value) {
                      caps = caps.Union(
                          ExpressionCapabilityUsage(*field.value));
                    }
                  }
                }
              },
              node.args);
          return caps;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.receiver) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.receiver));
          }
          for (const auto& arg : node.args) {
            if (arg.value) {
              caps = caps.Union(ExpressionCapabilityUsage(*arg.value));
            }
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          if (node.base) {
            return ExpressionCapabilityUsage(*node.base);
          }
          return CapabilitySet::Empty();
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.base) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.base));
          }
          if (node.index) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.index));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            return BlockUsesCapabilities(*node.block);
          }
          return CapabilitySet::Empty();
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.cond) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.cond));
          }
          if (node.then_expr) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.then_expr));
          }
          if (node.else_expr) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.else_expr));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.scrutinee) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.scrutinee));
          }
          for (const auto& case_clause : node.cases) {
            if (case_clause.body) {
              caps = caps.Union(ExpressionCapabilityUsage(*case_clause.body));
            }
          }
          if (node.else_expr) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.else_expr));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.scrutinee) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.scrutinee));
          }
          if (node.then_expr) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.then_expr));
          }
          if (node.else_expr) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.else_expr));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            return BlockUsesCapabilities(*node.body);
          }
          return CapabilitySet::Empty();
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.cond) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.cond));
          }
          if (node.body) {
            caps = caps.Union(BlockUsesCapabilities(*node.body));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.iter) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.iter));
          }
          if (node.body) {
            caps = caps.Union(BlockUsesCapabilities(*node.body));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CapabilitySet caps = CapabilitySet::Empty();
          if (node.lhs) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.lhs));
          }
          if (node.rhs) {
            caps = caps.Union(ExpressionCapabilityUsage(*node.rhs));
          }
          return caps;
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          if (node.value) {
            return ExpressionCapabilityUsage(*node.value);
          }
          return CapabilitySet::Empty();
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          CapabilitySet set{};
          set.Add(CapabilityKind::ExecutionDomain);
          if (node.body) {
            set = set.Union(BlockUsesCapabilities(*node.body));
          }
          return set;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          CapabilitySet set{};
          set.Add(CapabilityKind::ExecutionDomain);
          if (node.body) {
            set = set.Union(BlockUsesCapabilities(*node.body));
          }
          return set;
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          CapabilitySet set{};
          set.Add(CapabilityKind::ExecutionDomain);
          if (node.body) {
            set = set.Union(BlockUsesCapabilities(*node.body));
          }
          return set;
        } else {
          return CapabilitySet::Empty();
        }
      },
      expr.node);
}

CapabilitySet ExpressionUsesCapabilities(const ast::Expr& expr) {
  return ExpressionCapabilityUsage(expr);
}

}  // namespace ultraviolet::analysis

