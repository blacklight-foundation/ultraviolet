// =============================================================================
// MIGRATION MAPPING: drop_hooks.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.4 Expression Lowering - Drop Glue (lines 15847-15870)
//   - ExecIR-DropGlue rule (lines 15847-15850)
//   - Drop::drop method invocation (lines 15851-15860)
//   - DropGlue symbol generation (lines 15861-15870)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/cleanup.cpp
//   - Lines 801-1000: DropGlueSym generation
//   - Lines 1001-1100: EmitDropGlue procedure generation
//   - Lines 1101-1200: Drop hook registration
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/cleanup/drop_hooks.h
//   - cursive/include/05_codegen/ir/ir_model.h (ProcIR)
//   - cursive/include/05_codegen/symbols/mangle.h (MangleDrop)
//   - cursive/include/04_analysis/types/types.h (DropType predicate)
//
// REFACTORING NOTES:
//   1. Drop glue is a compiler-generated procedure for type cleanup
//   2. Generated once per type that requires Drop
//   3. Symbol is mangled: DropGlueSym(T) = "__drop_" + MangleType(T)
//   4. Takes pointer to value as parameter
//   5. Calls user-defined Drop::drop if present
//   6. Then recursively drops fields
//   7. Used by cleanup paths and assignment drops
//
// DROP GLUE STRUCTURE:
//   proc __drop_TypeName(ptr: *mut T) -> () {
//     // Call user Drop::drop if defined
//     if HasUserDrop(T) {
//       T::drop(ptr)
//     }
//     // Drop fields in reverse order
//     DropFields(ptr)
//   }
//
// DROP HOOK TYPES:
//   - UserDrop: User-defined Drop::drop method
//   - FieldDrop: Recursive field cleanup
//   - ArrayDrop: Element-by-element cleanup
//   - UnionDrop: Active variant cleanup (requires tag check)
//   - ModalDrop: Current state cleanup
// =============================================================================

#include "05_codegen/cleanup/drop_hooks.h"

#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/unwind.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/symbols/mangle.h"
#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_expr.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace cursive::codegen {

// ============================================================================
// Helper functions
// ============================================================================

// Helper to check if a TypeRef holds a specific type
template <typename T>
static bool HoldsType(const analysis::TypeRef& type) {
  return type && std::holds_alternative<T>(type->node);
}

template <typename T>
static const T& GetType(const analysis::TypeRef& type) {
  return std::get<T>(type->node);
}

static ast::Path ToSyntaxPath(const analysis::TypePath& path) {
  ast::Path out;
  out.reserve(path.size());
  for (const auto& segment : path) {
    out.push_back(segment);
  }
  return out;
}

static std::string DropCacheModuleKey(const std::vector<std::string>& path) {
  std::string out;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      out += "::";
    }
    out += path[i];
  }
  return out;
}

static std::string DropNeedCacheKey(const analysis::TypeRef& stripped,
                                    const LowerCtx& ctx) {
  return std::to_string(reinterpret_cast<std::uintptr_t>(ctx.sigma)) + "|" +
         DropCacheModuleKey(ctx.module_path) + "|" +
         (stripped ? analysis::TypeToString(stripped) : std::string());
}

static std::unordered_map<std::string, bool>& DropNeedCache(LowerCtx& ctx) {
  if (!ctx.values.drop_need_cache) {
    ctx.values.drop_need_cache =
        std::make_shared<std::unordered_map<std::string, bool>>();
  }
  return *ctx.values.drop_need_cache;
}

static const std::unordered_map<std::string, bool>* DropNeedCache(
    const LowerCtx& ctx) {
  return ctx.values.drop_need_cache.get();
}

static std::optional<analysis::TypeRef> LowerTypeForDrop(
    const std::shared_ptr<ast::Type>& type,
    LowerCtx& ctx) {
  if (!type || !ctx.sigma) {
    return std::nullopt;
  }

  analysis::ScopeContext scope;
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = ctx.module_path;
  return analysis::layout::LowerTypeForLayout(scope, type);
}

static analysis::TypeRef InstantiateIfGeneric(
    analysis::TypeRef type,
    const std::optional<ast::GenericParams>& generic_params,
    const std::vector<analysis::TypeRef>& generic_args) {
  if (!type || !generic_params.has_value() || generic_params->params.empty()) {
    return type;
  }
  if (generic_args.size() > generic_params->params.size()) {
    return nullptr;
  }

  const auto subst =
      analysis::BuildSubstitution(generic_params->params, generic_args);
  if (subst.empty()) {
    return type;
  }
  return analysis::InstantiateType(type, subst);
}

static const ast::StateBlock* FindModalState(
    const ast::ModalDecl& decl,
    std::string_view name) {
  for (const auto& state : decl.states) {
    if (analysis::IdEq(state.name, name)) {
      return &state;
    }
  }
  return nullptr;
}

static bool TypeNeedsDropImpl(const analysis::TypeRef& type,
                              LowerCtx& ctx,
                              std::unordered_set<std::string>& active);

static bool TypeNeedsDropCached(const analysis::TypeRef& type,
                                LowerCtx& ctx,
                                std::unordered_set<std::string>& active);

static bool StateFieldsNeedDrop(
    const ast::StateBlock& state,
    const std::optional<ast::GenericParams>& generic_params,
    const std::vector<analysis::TypeRef>& generic_args,
    LowerCtx& ctx,
    std::unordered_set<std::string>& active) {
  for (const auto& member : state.members) {
    const auto* field = std::get_if<ast::StateFieldDecl>(&member);
    if (!field) {
      continue;
    }
    auto lowered = LowerTypeForDrop(field->type, ctx);
    if (!lowered.has_value()) {
      return true;
    }
    analysis::TypeRef field_type =
        InstantiateIfGeneric(*lowered, generic_params, generic_args);
    if (!field_type || TypeNeedsDropCached(field_type, ctx, active)) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// Section 6.8 / Section 7.4 Drop Hook Registry
// ============================================================================

void DropHookRegistry::Register(const std::string& type_key, DropHook hook) {
  hooks[type_key] = std::move(hook);
}

std::optional<DropHook> DropHookRegistry::Lookup(const std::string& type_key) const {
  auto it = hooks.find(type_key);
  if (it == hooks.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool DropHookRegistry::HasHook(const std::string& type_key) const {
  return hooks.find(type_key) != hooks.end();
}

void DropHookRegistry::Clear() {
  hooks.clear();
}

// ============================================================================
// Section 6.8 Drop Hook Lookup
// ============================================================================

std::string DropTypeKey(const analysis::TypeRef& type) {
  SPEC_RULE("DropTypeKey");

  if (!type) {
    return "";
  }

  // Use TypeToString for the key (handles all type forms)
  return analysis::TypeToString(type);
}

std::optional<std::string> LookupDropMethodSymbol(
    const analysis::TypeRef& type,
    LowerCtx& ctx) {
  SPEC_RULE("LookupDropMethodSymbol");

  if (!ctx.sigma) {
    return std::nullopt;
  }

  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }

  analysis::ScopeContext scope;
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = ctx.module_path;

  ast::ClassPath drop_path;
  drop_path.push_back("Drop");
  if (!analysis::TypeImplementsClass(scope, stripped, drop_path)) {
    return std::nullopt;
  }

  return MethodSymbol(scope, stripped, "drop");
}

bool IsBuiltinDropType(const analysis::TypeRef& type) {
  SPEC_RULE("IsBuiltinDropType");

  if (!type) {
    return false;
  }

  // Check for string@Managed
  if (HoldsType<analysis::TypeString>(type)) {
    const auto& str_type = GetType<analysis::TypeString>(type);
    return str_type.state.has_value() &&
           *str_type.state == analysis::StringState::Managed;
  }

  // Check for bytes@Managed
  if (HoldsType<analysis::TypeBytes>(type)) {
    const auto& bytes_type = GetType<analysis::TypeBytes>(type);
    return bytes_type.state.has_value() &&
           *bytes_type.state == analysis::BytesState::Managed;
  }

  return false;
}

static bool TypeNeedsDropCached(const analysis::TypeRef& type,
                                LowerCtx& ctx,
                                std::unordered_set<std::string>& active) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (stripped && HoldsType<analysis::TypeRefine>(stripped)) {
    stripped = GetType<analysis::TypeRefine>(stripped).base;
  }
  const std::string active_key =
      stripped ? analysis::TypeToString(stripped) : std::string();
  if (!active_key.empty() && active.find(active_key) != active.end()) {
    return false;
  }

  const std::string cache_key = DropNeedCacheKey(stripped, ctx);
  auto& cache = DropNeedCache(ctx);
  auto cache_it = cache.find(cache_key);
  if (cache_it != cache.end()) {
    return cache_it->second;
  }
  if (ctx.values.parent != nullptr) {
    const auto* parent_cache = DropNeedCache(*ctx.values.parent);
    if (parent_cache != nullptr) {
      auto parent_it = parent_cache->find(cache_key);
      if (parent_it != parent_cache->end()) {
        return parent_it->second;
      }
    }
  }

  const bool needs_drop = TypeNeedsDropImpl(type, ctx, active);
  cache[cache_key] = needs_drop;
  return needs_drop;
}

static bool TypeNeedsDropImpl(const analysis::TypeRef& type,
                              LowerCtx& ctx,
                              std::unordered_set<std::string>& active) {
  if (!type) {
    return false;
  }

  // Strip permission
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return false;
  }

  if (HoldsType<analysis::TypeRefine>(stripped)) {
    const auto& refined = GetType<analysis::TypeRefine>(stripped);
    return TypeNeedsDropCached(refined.base, ctx, active);
  }

  const std::string active_key = analysis::TypeToString(stripped);
  if (!active.insert(active_key).second) {
    return false;
  }
  struct ActiveGuard {
    std::unordered_set<std::string>& active;
    std::string key;
    ~ActiveGuard() {
      active.erase(key);
    }
  } guard{active, active_key};

  // Built-in drop types
  if (IsBuiltinDropType(stripped)) {
    return true;
  }

  // Check for user-defined Drop method
  auto sym = LookupDropMethodSymbol(stripped, ctx);
  if (sym.has_value()) {
    return true;
  }

  // Primitives don't need drop
  if (HoldsType<analysis::TypePrim>(stripped)) {
    return false;
  }

  // Raw pointers don't need drop
  if (HoldsType<analysis::TypeRawPtr>(stripped)) {
    return false;
  }

  // Safe pointers don't need drop (they're borrowed)
  if (HoldsType<analysis::TypePtr>(stripped)) {
    return false;
  }

  // Ranges don't need drop
  if (analysis::IsRangeType(stripped)) {
    return false;
  }

  // Function types don't need drop
  if (HoldsType<analysis::TypeFunc>(stripped)) {
    return false;
  }

  // Slices don't need drop (borrowed)
  if (HoldsType<analysis::TypeSlice>(stripped)) {
    return false;
  }

  // String@View doesn't need drop
  if (HoldsType<analysis::TypeString>(stripped)) {
    const auto& str_type = GetType<analysis::TypeString>(stripped);
    if (!str_type.state.has_value() ||
        *str_type.state == analysis::StringState::View) {
      return false;
    }
  }

  // Bytes@View doesn't need drop
  if (HoldsType<analysis::TypeBytes>(stripped)) {
    const auto& bytes_type = GetType<analysis::TypeBytes>(stripped);
    if (!bytes_type.state.has_value() ||
        *bytes_type.state == analysis::BytesState::View) {
      return false;
    }
  }

  // Arrays need drop if elements need drop
  if (HoldsType<analysis::TypeArray>(stripped)) {
    const auto& arr_type = GetType<analysis::TypeArray>(stripped);
    return arr_type.length != 0 &&
           TypeNeedsDropCached(arr_type.element, ctx, active);
  }

  // Tuples need drop if any element needs drop
  if (HoldsType<analysis::TypeTuple>(stripped)) {
    const auto& tuple_type = GetType<analysis::TypeTuple>(stripped);
    for (const auto& elem : tuple_type.elements) {
      if (TypeNeedsDropCached(elem, ctx, active)) {
        return true;
      }
    }
    return false;
  }

  // Unions need drop if any member needs drop
  if (HoldsType<analysis::TypeUnion>(stripped)) {
    const auto& uni_type = GetType<analysis::TypeUnion>(stripped);
    for (const auto& member : uni_type.members) {
      if (TypeNeedsDropCached(member, ctx, active)) {
        return true;
      }
    }
    return false;
  }

  auto nominal_needs_drop =
      [&](const analysis::TypePath& path,
          const std::vector<analysis::TypeRef>& generic_args) -> bool {
    if (!ctx.sigma) {
      return true;
    }

    const ast::Path syntax_path = ToSyntaxPath(path);
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
    if (it == ctx.sigma->types.end()) {
      return true;
    }

    if (const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second)) {
      auto lowered = LowerTypeForDrop(alias->type, ctx);
      if (!lowered.has_value()) {
        return true;
      }
      analysis::TypeRef alias_type =
          InstantiateIfGeneric(*lowered, alias->generic_params, generic_args);
      return !alias_type || TypeNeedsDropCached(alias_type, ctx, active);
    }

    if (const auto* record = std::get_if<ast::RecordDecl>(&it->second)) {
      for (const auto& member : record->members) {
        const auto* field = std::get_if<ast::FieldDecl>(&member);
        if (!field) {
          continue;
        }
        auto lowered = LowerTypeForDrop(field->type, ctx);
        if (!lowered.has_value()) {
          return true;
        }
        analysis::TypeRef field_type =
            InstantiateIfGeneric(*lowered, record->generic_params, generic_args);
        if (!field_type || TypeNeedsDropCached(field_type, ctx, active)) {
          return true;
        }
      }
      return false;
    }

    if (const auto* enum_decl = std::get_if<ast::EnumDecl>(&it->second)) {
      for (const auto& variant : enum_decl->variants) {
        if (!variant.payload_opt.has_value()) {
          continue;
        }
        if (const auto* tuple_payload =
                std::get_if<ast::VariantPayloadTuple>(&*variant.payload_opt)) {
          for (const auto& element : tuple_payload->elements) {
            auto lowered = LowerTypeForDrop(element, ctx);
            if (!lowered.has_value()) {
              return true;
            }
            analysis::TypeRef payload_type =
                InstantiateIfGeneric(*lowered, enum_decl->generic_params, generic_args);
            if (!payload_type || TypeNeedsDropCached(payload_type, ctx, active)) {
              return true;
            }
          }
        } else if (const auto* record_payload =
                       std::get_if<ast::VariantPayloadRecord>(
                           &*variant.payload_opt)) {
          for (const auto& field : record_payload->fields) {
            auto lowered = LowerTypeForDrop(field.type, ctx);
            if (!lowered.has_value()) {
              return true;
            }
            analysis::TypeRef payload_type =
                InstantiateIfGeneric(*lowered, enum_decl->generic_params, generic_args);
            if (!payload_type || TypeNeedsDropCached(payload_type, ctx, active)) {
              return true;
            }
          }
        }
      }
      return false;
    }

    if (const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second)) {
      for (const auto& state : modal_decl->states) {
        if (StateFieldsNeedDrop(
                state,
                modal_decl->generic_params,
                generic_args,
                ctx,
                active)) {
          return true;
        }
      }
      return false;
    }

    return true;
  };

  if (HoldsType<analysis::TypePathType>(stripped)) {
    const auto& path_type = GetType<analysis::TypePathType>(stripped);
    return nominal_needs_drop(path_type.path, path_type.generic_args);
  }

  if (HoldsType<analysis::TypeApply>(stripped)) {
    const auto& apply_type = GetType<analysis::TypeApply>(stripped);
    return nominal_needs_drop(apply_type.path, apply_type.args);
  }

  if (HoldsType<analysis::TypeModalState>(stripped)) {
    const auto& state_type = GetType<analysis::TypeModalState>(stripped);
    if (!ctx.sigma) {
      return true;
    }
    const ast::Path syntax_path = ToSyntaxPath(state_type.path);
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
    if (it == ctx.sigma->types.end()) {
      return true;
    }
    const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second);
    if (!modal_decl) {
      return true;
    }
    const ast::StateBlock* state =
        FindModalState(*modal_decl, state_type.state);
    if (!state) {
      return true;
    }
    return StateFieldsNeedDrop(
        *state,
        modal_decl->generic_params,
        state_type.generic_args,
        ctx,
        active);
  }

  // Dynamic types don't need drop (no Drop on capabilities)
  if (HoldsType<analysis::TypeDynamic>(stripped)) {
    return false;
  }

  return false;
}

bool TypeNeedsDrop(const analysis::TypeRef& type, LowerCtx& ctx) {
  SPEC_RULE("TypeNeedsDrop");

  std::unordered_set<std::string> active;
  return TypeNeedsDropCached(type, ctx, active);
}

// ============================================================================
// Section 6.8 Drop Hook Emission
// ============================================================================

IRPtr EmitDropHookCall(const DropHook& hook,
                       const IRValue& value,
                       LowerCtx& ctx) {
  SPEC_RULE("EmitDropHookCall");

  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = hook.symbol;
  call.args.push_back(value);

  if (hook.needs_panic_out) {
    IRValue panic_out;
    panic_out.kind = IRValue::Kind::Local;
    panic_out.name = std::string(kPanicOutName);
    call.args.push_back(panic_out);
  }

  return MakeIR(std::move(call));
}

DropHook GetOrCreateDropHook(const analysis::TypeRef& type, LowerCtx& ctx) {
  SPEC_RULE("GetOrCreateDropHook");

  DropHook hook;
  hook.type = type;

  // Check for builtin drop
  if (IsBuiltinDropType(type)) {
    if (HoldsType<analysis::TypeString>(type)) {
      hook.kind = DropHook::Kind::Builtin;
      hook.symbol = BuiltinSymStringDropManaged();
      hook.needs_panic_out = false;
      return hook;
    }
    if (HoldsType<analysis::TypeBytes>(type)) {
      hook.kind = DropHook::Kind::Builtin;
      hook.symbol = BuiltinSymBytesDropManaged();
      hook.needs_panic_out = false;
      return hook;
    }
  }

  // Check for user-defined Drop method
  auto sym = LookupDropMethodSymbol(type, ctx);
  if (sym.has_value()) {
    hook.kind = DropHook::Kind::MethodCall;
    hook.symbol = *sym;
    hook.needs_panic_out = ctx.NeedsPanicOutForSymbol(*sym);
    return hook;
  }

  // Generate drop glue
  hook.kind = DropHook::Kind::DropGlue;
  hook.symbol = DropGlueSym(type, ctx);
  hook.needs_panic_out = ctx.NeedsPanicOutForSymbol(hook.symbol);
  return hook;
}

// ============================================================================
// Section 7.4 DropCall - Invoke the drop method on a value
// ============================================================================

IRPtr EmitDropCall(const analysis::TypeRef& type,
                   const IRValue& value,
                   LowerCtx& ctx) {
  SPEC_RULE("EmitDropCall");

  // Get or create the drop hook
  DropHook hook = GetOrCreateDropHook(type, ctx);

  // Emit the call
  return EmitDropHookCall(hook, value, ctx);
}

// ============================================================================
// Section 7.4 DropChildren - Compute child values to drop
// ============================================================================

std::vector<DropChild> ComputeDropChildren(
    const analysis::TypeRef& type,
    const IRValue& value,
    const std::vector<std::string>& skip_fields,
    LowerCtx& ctx) {
  SPEC_RULE("ComputeDropChildren");

  std::vector<DropChild> children;

  if (!type || !ctx.sigma) {
    return children;
  }

  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return children;
  }

  // Arrays: reverse index order
  if (HoldsType<analysis::TypeArray>(stripped)) {
    const auto& arr_type = GetType<analysis::TypeArray>(stripped);
    for (std::size_t i = arr_type.length; i > 0; --i) {
      const std::size_t index = i - 1;
      DropChild child;
      child.type = arr_type.element;
      child.value.kind = IRValue::Kind::Opaque;
      child.value.name = value.name + "[" + std::to_string(index) + "]";
      ctx.RegisterValueType(child.value, arr_type.element);
      children.push_back(child);
    }
    return children;
  }

  // Tuples: reverse element order
  if (HoldsType<analysis::TypeTuple>(stripped)) {
    const auto& tuple_type = GetType<analysis::TypeTuple>(stripped);
    for (std::size_t i = tuple_type.elements.size(); i > 0; --i) {
      const std::size_t index = i - 1;
      DropChild child;
      child.type = tuple_type.elements[index];
      child.value.kind = IRValue::Kind::Opaque;
      child.value.name = value.name + "_" + std::to_string(index);
      ctx.RegisterValueType(child.value, tuple_type.elements[index]);
      children.push_back(child);
    }
    return children;
  }

  // Named types (records): get fields from declaration
  if (HoldsType<analysis::TypePathType>(stripped)) {
    const auto& type_path = GetType<analysis::TypePathType>(stripped);

    // Build skip set
    std::unordered_set<std::string> skip_set(skip_fields.begin(), skip_fields.end());

    // Convert to syntax path
    ast::Path syntax_path;
    syntax_path.reserve(type_path.path.size());
    for (const auto& seg : type_path.path) {
      syntax_path.push_back(seg);
    }

    // Look up record declaration
    const auto path_key = analysis::PathKeyOf(syntax_path);
    const auto it = ctx.sigma->types.find(path_key);
    if (it == ctx.sigma->types.end()) {
      return children;
    }

    const auto* record = std::get_if<ast::RecordDecl>(&it->second);
    if (!record) {
      return children;
    }

    // Collect fields in reverse order
    std::vector<const ast::FieldDecl*> fields;
    for (const auto& member : record->members) {
      if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
        fields.push_back(field);
      }
    }

    analysis::ScopeContext scope;
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = ctx.module_path;

    for (auto rit = fields.rbegin(); rit != fields.rend(); ++rit) {
      const ast::FieldDecl* field = *rit;

      // Skip if in skip set
      if (skip_set.count(field->name) > 0) {
        continue;
      }

      // Lower field type
      auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, field->type);
      if (!lowered.has_value()) {
        continue;
      }

      DropChild child;
      child.type = *lowered;
      child.value.kind = IRValue::Kind::Opaque;
      child.value.name = value.name + "." + field->name;
      ctx.RegisterValueType(child.value, *lowered);
      children.push_back(child);
    }
    return children;
  }

  return children;
}

IRPtr EmitDropChildList(const std::vector<DropChild>& children, LowerCtx& ctx) {
  SPEC_RULE("EmitDropChildList");

  if (children.empty()) {
    return EmptyIR();
  }

  std::vector<IRPtr> drops;
  drops.reserve(children.size());

  for (const auto& child : children) {
    drops.push_back(EmitDrop(child.type, child.value, ctx));
  }

  // Sequence with panic short-circuiting
  // (simpler version - just sequence them)
  return SeqIR(std::move(drops));
}

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorDropHookRules() {
  SPEC_RULE("DropTypeKey");
  SPEC_RULE("LookupDropMethodSymbol");
  SPEC_RULE("IsBuiltinDropType");
  SPEC_RULE("TypeNeedsDrop");
  SPEC_RULE("EmitDropHookCall");
  SPEC_RULE("GetOrCreateDropHook");
  SPEC_RULE("EmitDropCall");
  SPEC_RULE("ComputeDropChildren");
  SPEC_RULE("EmitDropChildList");
}

}  // namespace cursive::codegen


