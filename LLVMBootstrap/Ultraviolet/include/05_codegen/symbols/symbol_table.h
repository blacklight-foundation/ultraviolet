#pragma once

// =============================================================================
// Symbol Table for Code Generation
// =============================================================================
//
// This file provides a symbol table for tracking all generated symbols during
// code generation. The symbol table is used for:
//   - Duplicate detection
//   - Declaration before definition tracking
//   - External reference tracking
//   - Debug info generation
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.3 Symbols, Mangling, and Linkage (lines 15392-15663)
//   - VTableDecl, LiteralData, DefaultImpl constructors (lines 15397-15400)
//   - ScopedSym, RawSym (lines 15438-15439)
//
// =============================================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "04_analysis/typing/types.h"
#include "05_codegen/symbols/linkage.h"

namespace ultraviolet::codegen {

// =============================================================================
// Symbol Kinds
// =============================================================================

/// The kind of symbol being tracked.
enum class SymbolKind {
  /// User procedure or method.
  Procedure,
  /// Static variable (let/var at module level).
  Global,
  /// VTable for a class implementation.
  VTable,
  /// Literal data (string, bytes, array).
  Literal,
  /// Drop glue function.
  DropGlue,
  /// Default class method implementation.
  DefaultImpl,
  /// Module init function.
  InitFn,
  /// Module deinit function.
  DeinitFn,
  /// External (FFI) procedure.
  Extern,
  /// Entry point symbol.
  Entry,
  /// Runtime helper.
  Runtime,
};

// =============================================================================
// Symbol State
// =============================================================================

/// The definition state of a symbol.
enum class SymbolState {
  /// Symbol has been declared but not defined.
  Declared,
  /// Symbol has been defined (has body/value).
  Defined,
  /// Symbol is external (defined elsewhere).
  External,
};

// =============================================================================
// Symbol Info
// =============================================================================

/// Information about a single symbol.
struct SymbolInfo {
  /// The mangled name of the symbol.
  std::string name;

  /// The kind of symbol.
  SymbolKind kind = SymbolKind::Procedure;

  /// The linkage of the symbol.
  LinkageKind linkage = LinkageKind::Internal;

  /// The definition state.
  SymbolState state = SymbolState::Declared;

  /// The type of the symbol (for procedures: function type, for globals: value type).
  analysis::TypeRef type;

  /// For procedures: the return type.
  analysis::TypeRef return_type;

  /// For procedures: parameter types.
  std::vector<analysis::TypeRef> param_types;

  /// For literals: the raw bytes.
  std::vector<std::uint8_t> literal_bytes;

  /// For vtables: the class being implemented.
  analysis::TypePath class_path;

  /// For vtables: the concrete type implementing the class.
  analysis::TypeRef impl_type;

  /// Optional source location info for debugging.
  std::string source_file;
  std::uint32_t source_line = 0;
};

// =============================================================================
// Symbol Table
// =============================================================================

/// SymbolTable tracks all generated symbols for a module.
///
/// The symbol table provides:
///   - Symbol registration and lookup
///   - Declaration before definition tracking
///   - Duplicate detection
///   - Iteration over all symbols
class SymbolTable {
 public:
  SymbolTable() = default;
  ~SymbolTable() = default;

  // Non-copyable but movable
  SymbolTable(const SymbolTable&) = delete;
  SymbolTable& operator=(const SymbolTable&) = delete;
  SymbolTable(SymbolTable&&) = default;
  SymbolTable& operator=(SymbolTable&&) = default;

  // ===========================================================================
  // Symbol Declaration
  // ===========================================================================

  /// Declare a procedure symbol.
  /// Returns false if the symbol already exists with a different signature.
  bool declareProc(
      const std::string& name,
      LinkageKind linkage,
      const analysis::TypeRef& return_type,
      const std::vector<analysis::TypeRef>& param_types);

  /// Declare an external (FFI) procedure symbol.
  bool declareExtern(
      const std::string& name,
      const analysis::TypeRef& return_type,
      const std::vector<analysis::TypeRef>& param_types);

  /// Declare a global variable symbol.
  bool declareGlobal(
      const std::string& name,
      LinkageKind linkage,
      const analysis::TypeRef& type);

  /// Declare a VTable symbol.
  bool declareVTable(
      const std::string& name,
      const analysis::TypeRef& impl_type,
      const analysis::TypePath& class_path);

  /// Declare a literal data symbol.
  bool declareLiteral(
      const std::string& name,
      std::vector<std::uint8_t> bytes);

  /// Declare a drop glue symbol.
  bool declareDropGlue(
      const std::string& name,
      const analysis::TypeRef& type);

  /// Declare a default implementation symbol.
  bool declareDefaultImpl(
      const std::string& name,
      const analysis::TypeRef& impl_type,
      const analysis::TypePath& class_path,
      const std::string& method_name);

  /// Declare an init function symbol.
  bool declareInitFn(const std::string& name);

  /// Declare a deinit function symbol.
  bool declareDeinitFn(const std::string& name);

  /// Declare an entry point symbol.
  bool declareEntry(const std::string& name);

  /// Declare a runtime helper symbol.
  bool declareRuntime(const std::string& name);

  // ===========================================================================
  // Symbol Definition
  // ===========================================================================

  /// Mark a symbol as defined.
  /// Returns false if the symbol was not declared or is already defined.
  bool define(const std::string& name);

  /// Mark a symbol as external (defined elsewhere).
  bool markExternal(const std::string& name);

  // ===========================================================================
  // Symbol Lookup
  // ===========================================================================

  /// Look up a symbol by name.
  /// Returns nullptr if not found.
  const SymbolInfo* lookup(const std::string& name) const;

  /// Check if a symbol exists.
  bool exists(const std::string& name) const;

  /// Check if a symbol is defined (has body/value).
  bool isDefined(const std::string& name) const;

  /// Check if a symbol is declared but not defined.
  bool isDeclaredOnly(const std::string& name) const;

  /// Check if a symbol is external.
  bool isExternal(const std::string& name) const;

  // ===========================================================================
  // Iteration
  // ===========================================================================

  /// Get all symbols.
  const std::unordered_map<std::string, SymbolInfo>& symbols() const {
    return symbols_;
  }

  /// Get the number of symbols.
  std::size_t size() const { return symbols_.size(); }

  /// Check if empty.
  bool empty() const { return symbols_.empty(); }

  // ===========================================================================
  // Validation
  // ===========================================================================

  /// Check that all declared symbols are defined.
  /// Returns list of undefined symbols.
  std::vector<std::string> undefinedSymbols() const;

  /// Check for duplicate definitions.
  /// Returns list of duplicate symbols.
  std::vector<std::string> duplicateSymbols() const;

 private:
  /// Map from mangled name to symbol info.
  std::unordered_map<std::string, SymbolInfo> symbols_;

  /// Helper to add a symbol.
  bool addSymbol(SymbolInfo info);
};

}  // namespace ultraviolet::codegen
