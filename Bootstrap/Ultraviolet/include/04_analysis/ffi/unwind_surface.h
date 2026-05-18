#pragma once

// =============================================================================
// Unwind and FFI Surface Analysis
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 2.7 (Unwind and FFI Surface)
//
// This module provides utilities for analyzing FFI boundaries and unwind
// behavior in Ultraviolet programs. It is used at project level for:
// - Identifying procedures that cross FFI boundaries
// - Extracting unwind mode configuration from attributes
// - Collecting FFI surface information for linking decisions
//
// =============================================================================

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

// =============================================================================
// Unwind Mode
// =============================================================================
//
// SPEC RULE (2.7):
//   UnwindMode(proc) = m ⇔ UnwindAttr(proc) = m
//   UnwindMode(proc) = "abort" ⇔ UnwindAttr(proc) undefined
//
// Defines behavior when panics/unwinds cross FFI boundaries:
// - "abort": Program aborts if panic/unwind attempts to cross boundary
// - "catch": Foreign unwinds converted to Ultraviolet panics (imports),
//            Ultraviolet panics caught and converted to error indicator (exports)

enum class UnwindMode {
  Abort,  // Default: abort on unwind crossing
  Catch   // Catch and convert unwinds at boundary
};

// Convert UnwindMode to string representation
std::string_view UnwindModeToString(UnwindMode mode);

// Parse UnwindMode from string (returns nullopt if invalid)
std::optional<UnwindMode> ParseUnwindMode(std::string_view str);

// =============================================================================
// FFI Boundary Detection
// =============================================================================
//
// SPEC RULE (2.7):
//   FFIBoundary(proc) ⇔
//     proc = ExternProcDecl(...) ∨
//     (proc = ProcedureDecl(...) ∧ ExportAttr(proc) defined)
//
// A procedure crosses the FFI boundary if it is:
// 1. An extern procedure declaration (imported from foreign code)
// 2. A procedure with [[export]] attribute (exported to foreign code)

// Check if a procedure declaration has the [[export]] attribute
bool HasExportAttribute(const ast::ProcedureDecl& proc);

// Check if a procedure declaration has the [[unwind]] attribute
// If present, extracts the mode into mode_out
bool HasUnwindAttribute(const ast::ProcedureDecl& proc,
                        std::optional<UnwindMode>* mode_out);

// Check if an extern procedure has the [[unwind]] attribute
// If present, extracts the mode into mode_out
bool HasUnwindAttribute(const ast::ExternProcDecl& proc,
                        std::optional<UnwindMode>* mode_out);

// Get the unwind mode for a procedure (defaults to Abort if not specified)
UnwindMode GetUnwindMode(const ast::ProcedureDecl& proc);

// Get the unwind mode for an extern procedure (defaults to Abort if not specified)
UnwindMode GetUnwindMode(const ast::ExternProcDecl& proc);

// =============================================================================
// FFI Surface Information
// =============================================================================
//
// Collected information about FFI imports and exports in a module.
// Used for link-time validation and symbol visibility decisions.

// Information about an imported extern procedure.
struct FfiImportInfo {
  std::string name;
  std::string abi;       // ABI string (e.g., "C", "C-unwind")
  UnwindMode unwind_mode;
  core::Span span;
};

// Information about an exported procedure
struct ExportProcInfo {
  std::string name;
  UnwindMode unwind_mode;
  core::Span span;
};

// Collected FFI surface for a module
struct FfiSurfaceInfo {
  std::vector<FfiImportInfo> imports;    // Extern procedure declarations
  std::vector<ExportProcInfo> exports;   // Procedures with [[export]] attribute
};

// Collect FFI surface information from a module
FfiSurfaceInfo CollectFfiSurface(const ast::ASTModule& module);

// Collect FFI surface information from a file
FfiSurfaceInfo CollectFfiSurface(const ast::ASTFile& file);

}  // namespace ultraviolet::analysis
