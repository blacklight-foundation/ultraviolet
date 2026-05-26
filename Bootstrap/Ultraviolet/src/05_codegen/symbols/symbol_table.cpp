// =============================================================================
// Symbol Table Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.3 Symbols, Mangling, and Linkage (lines 15392-15663)
//   - Symbols track: mangled name, linkage, type info
//   - VTableDecl, LiteralData, DefaultImpl constructors (lines 15397-15400)
//   - ScopedSym, RawSym (lines 15438-15439)
//
// =============================================================================

#include "05_codegen/symbols/symbol_table.h"

namespace ultraviolet::codegen {

// =============================================================================
// Helper Implementation
// =============================================================================

bool SymbolTable::addSymbol(SymbolInfo info) {
  const std::string& name = info.name;
  auto it = symbols_.find(name);
  if (it != symbols_.end()) {
    // Symbol already exists - this is a duplicate
    return false;
  }
  symbols_.emplace(name, std::move(info));
  return true;
}

// =============================================================================
// Symbol Declaration Implementation
// =============================================================================

bool SymbolTable::declareProc(
    const std::string& name,
    LinkageKind linkage,
    const analysis::TypeRef& return_type,
    const std::vector<analysis::TypeRef>& param_types) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Procedure;
  info.linkage = linkage;
  info.state = SymbolState::Declared;
  info.return_type = return_type;
  info.param_types = param_types;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareExtern(
    const std::string& name,
    const analysis::TypeRef& return_type,
    const std::vector<analysis::TypeRef>& param_types) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Extern;
  info.linkage = LinkageKind::External;
  info.state = SymbolState::External;
  info.return_type = return_type;
  info.param_types = param_types;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareGlobal(
    const std::string& name,
    LinkageKind linkage,
    const analysis::TypeRef& type) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Global;
  info.linkage = linkage;
  info.state = SymbolState::Declared;
  info.type = type;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareVTable(
    const std::string& name,
    const analysis::TypeRef& impl_type,
    const analysis::TypePath& class_path) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::VTable;
  info.linkage = LinkageOfVTable();
  info.state = SymbolState::Declared;
  info.impl_type = impl_type;
  info.class_path = class_path;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareLiteral(
    const std::string& name,
    std::vector<std::uint8_t> bytes) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Literal;
  info.linkage = LinkageOfLiteral();
  info.state = SymbolState::Defined;  // Literals are always defined
  info.literal_bytes = std::move(bytes);
  return addSymbol(std::move(info));
}

bool SymbolTable::declareDropGlue(
    const std::string& name,
    const analysis::TypeRef& type) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::DropGlue;
  info.linkage = LinkageOfDropGlue();
  info.state = SymbolState::Declared;
  info.type = type;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareDefaultImpl(
    const std::string& name,
    const analysis::TypeRef& impl_type,
    const analysis::TypePath& class_path,
    [[maybe_unused]] const std::string& method_name) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::DefaultImpl;
  // Default impl linkage follows method visibility - use internal for now
  info.linkage = LinkageKind::Internal;
  info.state = SymbolState::Declared;
  info.impl_type = impl_type;
  info.class_path = class_path;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareInitFn(const std::string& name) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::InitFn;
  info.linkage = LinkageOfInitFn();
  info.state = SymbolState::Declared;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareDeinitFn(const std::string& name) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::DeinitFn;
  info.linkage = LinkageOfDeinitFn();
  info.state = SymbolState::Declared;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareEntry(const std::string& name) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Entry;
  info.linkage = LinkageOfEntrySym();
  info.state = SymbolState::Declared;
  return addSymbol(std::move(info));
}

bool SymbolTable::declareRuntime(const std::string& name) {
  SymbolInfo info;
  info.name = name;
  info.kind = SymbolKind::Runtime;
  info.linkage = LinkageKind::Internal;
  info.state = SymbolState::Declared;
  return addSymbol(std::move(info));
}

// =============================================================================
// Symbol Definition Implementation
// =============================================================================

bool SymbolTable::define(const std::string& name) {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    // Symbol was not declared
    return false;
  }
  if (it->second.state == SymbolState::Defined) {
    // Already defined - this is a redefinition error
    return false;
  }
  if (it->second.state == SymbolState::External) {
    // Cannot define an external symbol
    return false;
  }
  it->second.state = SymbolState::Defined;
  return true;
}

bool SymbolTable::markExternal(const std::string& name) {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return false;
  }
  it->second.state = SymbolState::External;
  return true;
}

// =============================================================================
// Symbol Lookup Implementation
// =============================================================================

const SymbolInfo* SymbolTable::lookup(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool SymbolTable::exists(const std::string& name) const {
  return symbols_.find(name) != symbols_.end();
}

bool SymbolTable::isDefined(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return false;
  }
  return it->second.state == SymbolState::Defined;
}

bool SymbolTable::isDeclaredOnly(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return false;
  }
  return it->second.state == SymbolState::Declared;
}

bool SymbolTable::isExternal(const std::string& name) const {
  auto it = symbols_.find(name);
  if (it == symbols_.end()) {
    return false;
  }
  return it->second.state == SymbolState::External;
}

// =============================================================================
// Validation Implementation
// =============================================================================

std::vector<std::string> SymbolTable::undefinedSymbols() const {
  std::vector<std::string> undefined;
  for (const auto& [name, info] : symbols_) {
    if (info.state == SymbolState::Declared) {
      // Skip external symbols - they are allowed to be undefined locally
      if (info.kind != SymbolKind::Extern) {
        undefined.push_back(name);
      }
    }
  }
  return undefined;
}

std::vector<std::string> SymbolTable::duplicateSymbols() const {
  // Since we prevent duplicates on insertion, this returns empty.
  // If we tracked duplicates, we would return them here.
  return {};
}

}  // namespace ultraviolet::codegen
