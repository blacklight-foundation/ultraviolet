// =============================================================================
// File: 04_analysis/ffi/unwind_surface.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 2.7 (lines 1683-1701)
//   - 2.7. Unwind and FFI Surface
//   - FFI Boundary definition
//   - Unwind Modes
//   - Boundary Effects
//   - Safety requirements for extern calls
//
// =============================================================================

#include "04_analysis/ffi/unwind_surface.h"

#include <string>
#include <variant>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"

namespace ultraviolet::analysis {

namespace {

// Extract ABI string from ExternAbi variant
std::string GetAbiString(const std::optional<ast::ExternAbi>& abi_opt) {
  if (!abi_opt.has_value()) {
    return "C";  // Default ABI if not specified
  }

  return std::visit(
      [](const auto& abi) -> std::string {
        using T = std::decay_t<decltype(abi)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          // Extract string value from literal token
          std::string_view lit = abi.literal.lexeme;
          // Remove quotes if present
          if (lit.size() >= 2 && lit.front() == '"' && lit.back() == '"') {
            return std::string(lit.substr(1, lit.size() - 2));
          }
          return std::string(lit);
        } else if constexpr (std::is_same_v<T, ast::ExternAbiIdent>) {
          return std::string(abi.name);
        } else {
          return "C";
        }
      },
      *abi_opt);
}

// Helper to extract unwind mode from attribute list
std::optional<UnwindMode> ExtractUnwindModeFromAttrs(
    const ast::AttributeList& attrs) {
  const ast::AttributeItem* unwind_attr =
      ast::find_attribute(attrs, analysis::attrs::kUnwind);
  if (!unwind_attr) {
    return std::nullopt;
  }

  if (unwind_attr->args.size() != 1 || unwind_attr->args.front().key.has_value()) {
    return std::nullopt;
  }

  // Get the first positional argument
  auto token_opt = ast::get_attr_token_arg(*unwind_attr, 0);
  if (!token_opt.has_value() ||
      token_opt->kind != lexer::TokenKind::StringLiteral) {
    return std::nullopt;
  }

  // Parse the mode from the token lexeme
  std::string_view mode_str = token_opt->lexeme;
  // Remove quotes if it's a string literal
  if (mode_str.size() >= 2 && mode_str.front() == '"' && mode_str.back() == '"') {
    mode_str = mode_str.substr(1, mode_str.size() - 2);
  }

  return ParseUnwindMode(mode_str);
}

// Process a single ASTItem and add FFI info if applicable
void ProcessItem(const ast::ASTItem& item,
                 FfiSurfaceInfo& surface) {
  std::visit(
      [&surface](const auto& decl) {
        using T = std::decay_t<decltype(decl)>;

        if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          // Check for [[export]] attribute
          if (ast::has_attribute(decl.attrs, analysis::attrs::kExport)) {
            SPEC_RULE("FFIBoundary");
            ExportProcInfo info;
            info.name = std::string(decl.name);
            info.unwind_mode = GetUnwindMode(decl);
            info.span = decl.span;
            surface.exports.push_back(std::move(info));
          }
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          // Process all extern procedures in the block
          for (const auto& extern_item : decl.items) {
            std::visit(
                [&surface, &decl](const auto& extern_decl) {
                  using ET = std::decay_t<decltype(extern_decl)>;
                  if constexpr (std::is_same_v<ET, ast::ExternProcDecl>) {
                    SPEC_RULE("FFIBoundary");
                    FfiImportInfo info;
                    info.name = std::string(extern_decl.name);
                    info.abi = GetAbiString(decl.abi_opt);
                    info.unwind_mode = GetUnwindMode(extern_decl);
                    info.span = extern_decl.span;
                    surface.imports.push_back(std::move(info));
                  }
                },
                extern_item);
          }
        }
        // Other item types are not FFI boundaries
      },
      item);
}

}  // namespace

std::string_view UnwindModeToString(UnwindMode mode) {
  switch (mode) {
    case UnwindMode::Abort:
      return "abort";
    case UnwindMode::Catch:
      return "catch";
  }
  return "abort";  // Fallback
}

std::optional<UnwindMode> ParseUnwindMode(std::string_view str) {
  if (str == "abort") {
    return UnwindMode::Abort;
  }
  if (str == "catch") {
    return UnwindMode::Catch;
  }
  return std::nullopt;
}

bool HasExportAttribute(const ast::ProcedureDecl& proc) {
  return ast::has_attribute(proc.attrs, analysis::attrs::kExport);
}

bool HasUnwindAttribute(const ast::ProcedureDecl& proc,
                        std::optional<UnwindMode>* mode_out) {
  const ast::AttributeItem* attr =
      ast::find_attribute(proc.attrs, analysis::attrs::kUnwind);
  if (!attr) {
    if (mode_out) {
      mode_out->reset();
    }
    return false;
  }

  if (mode_out) {
    *mode_out = ExtractUnwindModeFromAttrs(proc.attrs);
  }
  return true;
}

bool HasUnwindAttribute(const ast::ExternProcDecl& proc,
                        std::optional<UnwindMode>* mode_out) {
  const ast::AttributeItem* attr =
      ast::find_attribute(proc.attrs, analysis::attrs::kUnwind);
  if (!attr) {
    if (mode_out) {
      mode_out->reset();
    }
    return false;
  }

  if (mode_out) {
    *mode_out = ExtractUnwindModeFromAttrs(proc.attrs);
  }
  return true;
}

UnwindMode GetUnwindMode(const ast::ProcedureDecl& proc) {
  auto mode_opt = ExtractUnwindModeFromAttrs(proc.attrs);
  if (mode_opt.has_value()) {
    SPEC_RULE("UnwindMode-Explicit");
    return *mode_opt;
  }
  SPEC_RULE("UnwindMode-Default");
  return UnwindMode::Abort;
}

UnwindMode GetUnwindMode(const ast::ExternProcDecl& proc) {
  auto mode_opt = ExtractUnwindModeFromAttrs(proc.attrs);
  if (mode_opt.has_value()) {
    SPEC_RULE("UnwindMode-Explicit");
    return *mode_opt;
  }
  SPEC_RULE("UnwindMode-Default");
  return UnwindMode::Abort;
}

FfiSurfaceInfo CollectFfiSurface(const ast::ASTModule& module) {
  FfiSurfaceInfo surface;
  for (const auto& item : module.items) {
    ProcessItem(item, surface);
  }
  return surface;
}

FfiSurfaceInfo CollectFfiSurface(const ast::ASTFile& file) {
  FfiSurfaceInfo surface;
  for (const auto& item : file.items) {
    ProcessItem(item, surface);
  }
  return surface;
}

}  // namespace ultraviolet::analysis
