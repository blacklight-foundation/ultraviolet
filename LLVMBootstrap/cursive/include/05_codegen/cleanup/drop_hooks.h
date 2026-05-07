#pragma once

// Drop hook infrastructure for Cursive codegen.
// Provides registration and emission of drop hooks for types that need cleanup.
//
// This header is part of the cleanup subsystem (§6.8) and provides:
// - Drop hook registration for types with custom Drop implementations
// - Drop glue lookup and caching
// - Drop method lookup utilities

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "04_analysis/typing/types.h"

namespace cursive::codegen {

// Forward declarations
struct LowerCtx;

// ============================================================================
// §6.8 / §7.4 Drop Hook Types
// ============================================================================

// DropHook represents a registered drop implementation for a type.
// Per §7.4: DropType(T) <=> BuiltinDropType(T) || HasDropMethod(StripPerm(T))
struct DropHook {
  enum class Kind {
    Builtin,      // Built-in drop (string@Managed, bytes@Managed)
    MethodCall,   // User-defined drop method on a type
    DropGlue,     // Generated drop glue function
  };

  Kind kind = Kind::MethodCall;
  std::string symbol;                      // Symbol to call for drop
  analysis::TypeRef type;                  // Type being dropped
  std::vector<std::string> type_path;      // Path for named types
  bool needs_panic_out = false;            // Whether drop needs panic out param
};

// DropHookRegistry maintains a cache of drop hooks for types.
// Used to avoid repeated lookups during codegen.
struct DropHookRegistry {
  std::unordered_map<std::string, DropHook> hooks;

  // Register a drop hook for a type
  void Register(const std::string& type_key, DropHook hook);

  // Look up a registered drop hook
  std::optional<DropHook> Lookup(const std::string& type_key) const;

  // Check if a type has a registered drop hook
  bool HasHook(const std::string& type_key) const;

  // Clear all registered hooks
  void Clear();
};

// ============================================================================
// §6.8 Drop Hook Lookup
// ============================================================================

// Compute a unique key for a type (for registry lookups)
std::string DropTypeKey(const analysis::TypeRef& type);

// §7.4 HasDropMethod(T) - Check if type has a user-defined drop method
// Returns the symbol name if found
std::optional<std::string> LookupDropMethodSymbol(
    const analysis::TypeRef& type,
    LowerCtx& ctx);

// §7.4 BuiltinDropType(T) - Check if type has built-in drop
// Returns true for string@Managed, bytes@Managed
bool IsBuiltinDropType(const analysis::TypeRef& type);

// §7.4 DropType(T) - Check if type needs drop
bool TypeNeedsDrop(const analysis::TypeRef& type, LowerCtx& ctx);

// ============================================================================
// §6.8 Drop Hook Emission
// ============================================================================

// Emit a call to the drop hook for a type.
// This handles the different kinds of drop hooks (builtin, method, glue).
IRPtr EmitDropHookCall(const DropHook& hook,
                       const IRValue& value,
                       LowerCtx& ctx);

// Get or create the drop hook for a type.
// Registers the hook in the context's drop glue registry if needed.
DropHook GetOrCreateDropHook(const analysis::TypeRef& type, LowerCtx& ctx);

// ============================================================================
// §7.4 DropCall - Invoke the drop method on a value
// ============================================================================

// DropCall(T, v, sigma) - Call the drop method if the type has one
// Per §7.4: Dispatches to the appropriate drop implementation
IRPtr EmitDropCall(const analysis::TypeRef& type,
                   const IRValue& value,
                   LowerCtx& ctx);

// ============================================================================
// §7.4 DropChildren - Emit IR to drop child values
// ============================================================================

// Per §7.4:
// DropChildren(T, v, F) returns child (type, value) pairs to drop.
// - Records: [<T_i, v_i> | <f_i, T_i> in FieldsRev(R), f_i not in F]
// - Tuples: reverse order of elements
// - Arrays: reverse order of elements
// - Unions: payload based on discriminant
// - Modals: state-specific fields

struct DropChild {
  analysis::TypeRef type;
  IRValue value;
};

// Compute the list of children to drop for a composite type
std::vector<DropChild> ComputeDropChildren(
    const analysis::TypeRef& type,
    const IRValue& value,
    const std::vector<std::string>& skip_fields,
    LowerCtx& ctx);

// Emit IR to drop a list of children in order (with panic short-circuiting)
IRPtr EmitDropChildList(const std::vector<DropChild>& children, LowerCtx& ctx);

// ============================================================================
// Builtin Drop Symbols
// ============================================================================

// §6.9 Runtime symbols for builtin drop
std::string BuiltinSymStringDropManaged();
std::string BuiltinSymBytesDropManaged();

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorDropHookRules();

}  // namespace cursive::codegen
