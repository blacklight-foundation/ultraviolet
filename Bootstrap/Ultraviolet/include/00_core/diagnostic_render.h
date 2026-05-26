#pragma once

#include <functional>
#include <optional>
#include <string>

#include "00_core/diagnostics.h"

namespace ultraviolet::core {

enum class DiagnosticOrigin {
  Internal,
  External,
};

DiagnosticStream Order(const DiagnosticStream& stream);

Diagnostic ApplyNoSpanExternal(const Diagnostic& diag, DiagnosticOrigin origin);

std::string Render(const Diagnostic& diag);

// Rich diagnostic rendering with source context and optional ANSI color.
//
// SourceRegistry: callable that maps a file path to its full text content.
// Returns std::nullopt if the file cannot be loaded.
using SourceRegistry =
    std::function<std::optional<std::string>(const std::string&)>;

struct RenderOptions {
  bool color = false;
  int context_lines = 1;     // Lines of context before/after the error line
  int terminal_width = 0;    // 0 = no truncation
};

std::string RenderRich(const Diagnostic& diag,
                       const SourceRegistry& sources,
                       const RenderOptions& opts);

// Produce a summary line like "3 errors, 1 warning emitted".
std::string DiagnosticSummary(const DiagnosticStream& stream, bool color);

}  // namespace ultraviolet::core
