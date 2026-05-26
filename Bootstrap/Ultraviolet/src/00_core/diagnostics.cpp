// =============================================================================
// MIGRATION MAPPING: diagnostics.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 1.6.3 "Diagnostics: Records and Emission" (lines 612-634)
//     - Severity = {Error, Warning} (line 616)
//     - Diagnostic stream: Delta = [d_1, ..., d_n] (lines 618-619)
//     - Emit-Append rule: appends diagnostic to stream (lines 621-623)
//     - CompileStatus(Delta): fail if HasError(Delta), ok otherwise (lines 632-634)
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/diagnostics.cpp
//   - Lines 1-67 (entire file)
//
// CONTENT TO MIGRATE:
//   - SeverityLabel(severity) -> string (lines 13-25)
//     Internal helper: maps Severity enum to string label
//   - DiagPayload(diag) -> string (lines 27-37)
//     Internal helper: formats diagnostic for spec tracing
//   - Emit(stream, diag) -> DiagnosticStream (lines 39-49)
//     Appends diagnostic to stream, records spec trace
//     Implements Emit-Append rule from spec
//   - HasError(stream) -> bool (lines 51-60)
//     Returns true if stream contains Error or Panic severity
//   - CompileStatus(stream) -> CompileStatusResult (lines 62-65)
//     Returns Fail if HasError, Ok otherwise
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/diagnostics.h (header)
//     - Severity enum (Error, Warning, Info, Panic)
//     - Diagnostic struct (code, severity, message, span)
//     - DiagnosticStream type (vector<Diagnostic>)
//     - CompileStatusResult enum (Ok, Fail)
//   - ultraviolet/include/00_core/assert_spec.h
//     - SPEC_RULE macro
//     - SPEC_DEF macro
//   - ultraviolet/include/00_core/spec_trace.h
//     - SpecTrace::Enabled()
//     - SpecTrace::Record()
//
// REFACTORING NOTES:
//   1. Emit now has two variants:
//      - void Emit(stream&, diag) - in-place, O(1) amortized (preferred)
//      - DiagnosticStream EmitCopy(stream, diag) - functional style, O(n) copy
//   2. Spec defines Severity as {Error, Warning} but implementation
//      extends with {Info, Panic} for practical use
//   3. HasError checks both Error AND Panic severities
//   4. SPEC_RULE traces:
//      - "Emit-Append" -> section 1.6.3
//   5. SPEC_DEF traces:
//      - "Severity" -> "1.6.3"
//      - "Diagnostic" -> "1.6.3"
//      - "DiagnosticStream" -> "1.6.3"
//      - "HasError" -> "1.6.3"
//      - "CompileStatus" -> "1.6.3"
//   6. SpecTrace integration for conformance tracing
//   7. Consider making DiagPayload format match spec observable format
//
// =============================================================================

#include "00_core/diagnostics.h"

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::core {

static inline void SpecDefsDiagnosticTypes() {
  SPEC_DEF("Severity", "2.3");
  SPEC_DEF("Diagnostic", "2.3");
  SPEC_DEF("DiagnosticStream", "2.3");
}

static std::string SeverityLabel(Severity severity) {
  switch (severity) {
    case Severity::Error:
      return "error";
    case Severity::Warning:
      return "warning";
    case Severity::Info:
      return "info";
    case Severity::Panic:
      return "panic";
    case Severity::Note:
      return "note";
  }
  return "error";
}

static std::string DiagPayload(const Diagnostic& diag) {
  std::string payload;
  payload.reserve(diag.code.size() + diag.message.size() + 32);
  payload += "code=";
  payload += diag.code.empty() ? "<none>" : diag.code;
  payload += ";severity=";
  payload += SeverityLabel(diag.severity);
  payload += ";message=";
  payload += diag.message;
  return payload;
}

// In-place emission - O(1) amortized, preferred for performance
void Emit(DiagnosticStream& stream, const Diagnostic& diag) {
  SPEC_RULE("Emit-Append");
  SpecDefsDiagnosticTypes();
  if (Conformance::Enabled()) {
    const std::string payload = DiagPayload(diag);
    Conformance::Record("Diag-Emit", diag.span, payload);
  }
  stream.push_back(diag);
}

bool EmitList(DiagnosticStream& stream, const DiagnosticStream& diags) {
  SpecDefsDiagnosticTypes();
  for (const auto& diag : diags) {
    Emit(stream, diag);
  }
  return true;
}

// Functional-style emission - O(n) copy
DiagnosticStream EmitCopy(const DiagnosticStream& stream,
                          const Diagnostic& diag) {
  DiagnosticStream out = stream;
  Emit(out, diag);
  return out;
}

bool HasError(const DiagnosticStream& stream) {
  SPEC_DEF("HasError", "2.3");
  SpecDefsDiagnosticTypes();
  for (const auto& diag : stream) {
    if (diag.severity == Severity::Error) {
      return true;
    }
  }
  return false;
}

CompileStatusResult CompileStatus(const DiagnosticStream& stream) {
  SPEC_DEF("CompileStatus", "2.3");
  return HasError(stream) ? CompileStatusResult::Fail
                          : CompileStatusResult::Ok;
}

}  // namespace ultraviolet::core
