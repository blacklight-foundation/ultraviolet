#pragma once

#include <optional>
#include <string>
#include <vector>

#include "00_core/span.h"

namespace ultraviolet::core {

using DiagCode = std::string;

enum class Severity {
  Error,
  Warning,
  Info,
  Panic,
  Note,
};

// Sub-diagnostic attached to a primary Diagnostic (notes, help, fix-its).
enum class SubDiagnosticKind {
  Note,   // "note: previously declared here"
  Help,   // "help: use `~>` for method calls"
  FixIt,  // Machine-applicable replacement text
};

struct SubDiagnostic {
  SubDiagnosticKind kind = SubDiagnosticKind::Note;
  std::string message;
  std::optional<Span> span;
  std::optional<std::string> fix_text;  // Replacement text (FixIt only)
  std::optional<std::string> label;     // Inline label for caret line
};

struct Diagnostic {
  DiagCode code;  // Empty for auxiliary diagnostics with no code.
  Severity severity = Severity::Error;
  std::string message;
  std::optional<Span> span;
  std::optional<std::string> label;     // Inline label for primary caret line
  std::vector<SubDiagnostic> children;  // Notes, help labels, fix-its
};

using DiagnosticStream = std::vector<Diagnostic>;

enum class CompileStatusResult {
  Ok,
  Fail,
};

// In-place emission (preferred for performance - O(1) amortized)
void Emit(DiagnosticStream& stream, const Diagnostic& diag);
bool EmitList(DiagnosticStream& stream, const DiagnosticStream& diags);

// Functional-style emission (returns new stream - O(n) copy)
[[nodiscard]] DiagnosticStream EmitCopy(const DiagnosticStream& stream,
                                        const Diagnostic& diag);

bool HasError(const DiagnosticStream& stream);

CompileStatusResult CompileStatus(const DiagnosticStream& stream);

}  // namespace ultraviolet::core
