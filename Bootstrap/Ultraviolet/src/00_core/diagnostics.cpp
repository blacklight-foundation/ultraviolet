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
  SPEC_DEF("req.SliceDiagnosticOwnership", "12.4.7");
  SPEC_DEF("req.16.ControlExpressionDiagnosticOwnership", "16.7.7");
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

static void RecordDiagnosticObligation(const Diagnostic& diag,
                                       std::string_view obligation_id,
                                       const std::string& payload) {
  if (!obligation_id.empty()) {
    Conformance::Record(obligation_id, diag.span, payload);
  }
}

static bool IsChapter14RefinementTypeDiagnostic(std::string_view code) {
  return code == "E-TYP-1953" || code == "E-TYP-1954" ||
         code == "E-TYP-1955" || code == "E-TYP-1956" ||
         code == "E-TYP-1957" || code == "P-TYP-1953";
}

static bool IsChapter14RefinementPolymorphismDiagnostic(std::string_view code) {
  return IsChapter14RefinementTypeDiagnostic(code) ||
         code == "E-TYP-2301" || code == "E-TYP-2302" ||
         code == "E-TYP-2303" || code == "E-TYP-2304" ||
         code == "E-TYP-2305" || code == "E-TYP-2307" ||
         code == "E-TYP-2308" || code == "E-TYP-2401" ||
         code == "E-TYP-2402" || code == "E-TYP-2403" ||
         code == "E-TYP-2404" || code == "E-TYP-2405" ||
         code == "E-TYP-2406" || code == "E-TYP-2407" ||
         code == "E-TYP-2408" || code == "E-TYP-2409" ||
         code == "E-TYP-2500" || code == "E-TYP-2501" ||
         code == "E-TYP-2502" || code == "E-TYP-2503" ||
         code == "E-TYP-2504" || code == "E-TYP-2505" ||
         code == "E-TYP-2506" || code == "E-TYP-2507" ||
         code == "E-TYP-2508" || code == "E-TYP-2509" ||
         code == "E-TYP-2510" || code == "E-TYP-2511" ||
         code == "E-TYP-2512" || code == "E-TYP-2530" ||
         code == "E-TYP-2531" || code == "E-TYP-2540" ||
         code == "E-TYP-2541" || code == "E-TYP-2542" ||
         code == "E-TYP-2621" || code == "E-TYP-2622" ||
         code == "E-UNS-0105" || code == "E-UNS-0106";
}

static bool IsSourceLexicalDiagnostic(std::string_view code) {
  return code == "E-SRC-0101" || code == "E-SRC-0102" ||
         code == "E-SRC-0103" || code == "E-SRC-0104" ||
         code == "E-SRC-0301" || code == "E-SRC-0302" ||
         code == "E-SRC-0303" || code == "E-SRC-0304" ||
         code == "E-SRC-0306" || code == "E-SRC-0307" ||
         code == "E-SRC-0308" || code == "E-SRC-0309" ||
         code == "E-SRC-0310" || code == "E-SRC-0311" ||
         code == "W-SRC-0101" || code == "W-SRC-0301" ||
         code == "W-SRC-0308";
}

static bool IsChapter15ProcedureContractEntryDiagnostic(std::string_view code) {
  return code == "E-TYP-1507" || code == "E-TYP-1508" ||
         code == "E-TYP-1912" || code == "E-MOD-2411" ||
         code == "E-MOD-2430" || code == "E-MOD-2431" ||
         code == "E-MOD-2432" || code == "E-MOD-2434" ||
         code == "E-CON-0415" || code == "E-CON-0416" ||
         code == "P-SEM-2850" || code == "E-SEM-2801" ||
         code == "E-SEM-2802" || code == "E-SEM-2803" ||
         code == "E-SEM-2804" || code == "E-SEM-2805" ||
         code == "E-SEM-2806" || code == "E-SEM-2807" ||
         code == "E-SEM-2808" || code == "E-SEM-2820" ||
         code == "E-SEM-2821" || code == "E-SEM-2822" ||
         code == "E-SEM-2823" || code == "E-SEM-2824" ||
         code == "E-SEM-2830" || code == "E-SEM-2831" ||
         code == "E-SEM-3004";
}

static bool IsControlExpressionDiagnosticOwnershipDiagnostic(
    std::string_view code) {
  return code == "E-SEM-2705" || code == "E-SEM-2741" ||
         code == "E-TYP-2060" || code == "E-SEM-2751" ||
         code == "E-SEM-2830" || code == "E-SEM-2831";
}

static bool IsSliceDiagnosticOwnershipDiagnostic(std::string_view code) {
  return code == "E-TYP-1820" || code == "E-SEM-2527";
}

static void RecordDiagnosticTableObligations(const Diagnostic& diag,
                                             const std::string& payload) {
  const std::string_view code = diag.code;
  if (IsSliceDiagnosticOwnershipDiagnostic(code)) {
    RecordDiagnosticObligation(diag, "req.SliceDiagnosticOwnership", payload);
  }
  if (IsControlExpressionDiagnosticOwnershipDiagnostic(code)) {
    RecordDiagnosticObligation(
        diag, "req.16.ControlExpressionDiagnosticOwnership", payload);
  }
  if (IsSourceLexicalDiagnostic(code)) {
    RecordDiagnosticObligation(diag, "diagnostics.SourceLexicalDiagnostics",
                               payload);
  }
  if (code == "E-MOD-2450" || code == "E-MOD-2451" ||
      code == "E-MOD-2452") {
    RecordDiagnosticObligation(diag, "diagnostics.AttributeDiagnostics",
                               payload);
  }
  if (code == "E-CNF-0402") {
    RecordDiagnosticObligation(diag, "diagnostics.VendorAttributeDiagnostics",
                               payload);
  }
  if (code == "W-CNF-0601" || code == "E-CON-0410" ||
      code == "E-CON-0411" || code == "E-CON-0412" ||
      code == "W-CON-0401") {
    RecordDiagnosticObligation(
        diag, "diagnostics.DiagnosticsMetadataAttributes", payload);
  }
  if (code == "E-MOD-2453" || code == "E-MOD-2454" ||
      code == "E-MOD-2455" || code == "E-TYP-2105" ||
      code == "W-MOD-2451") {
    RecordDiagnosticObligation(diag, "diagnostics.LayoutAttributeDiagnostics",
                               payload);
  }
  if (code == "W-MOD-2452") {
    RecordDiagnosticObligation(
        diag, "diagnostics.OptimizationAttributeDiagnostics", payload);
  }
  if (code == "E-MOD-2452" || code == "E-TST-0101" ||
      code == "E-TST-0102" ||
      code == "E-TST-0103" || code == "E-TST-0104" ||
      code == "E-TST-0105" || code == "E-TST-0106" ||
      code == "E-TST-0107" || code == "E-TST-0108" ||
      code == "E-TST-0109") {
    RecordDiagnosticObligation(diag, "diagnostics.TestAttributes", payload);
  }
  if (code == "E-CNF-0401" || code == "E-SRC-0510" ||
      code == "E-SRC-0520" || code == "E-SRC-0521") {
    RecordDiagnosticObligation(diag, "diagnostics.ParsingDiagnostics",
                               payload);
  }
  if (code == "E-MOD-1202") {
    RecordDiagnosticObligation(diag, "diagnostics.ImportDeclarations",
                               payload);
  }
  if (code == "E-MOD-1104" || code == "E-MOD-1105" ||
      code == "E-MOD-1106" || code == "E-MOD-1201" ||
      code == "E-MOD-1304" || code == "E-MOD-1401" ||
      code == "W-MOD-1101") {
    RecordDiagnosticObligation(diag, "diagnostics.ModuleAggregation",
                               payload);
  }
  if (code == "E-MOD-1204" || code == "E-MOD-1205" ||
      code == "E-MOD-1206" || code == "W-MOD-1201") {
    RecordDiagnosticObligation(diag, "diagnostics.UsingDeclarations",
                               payload);
  }
  if (code == "E-CNF-0406" || code == "E-MOD-1203" ||
      code == "E-MOD-1207" || code == "E-MOD-1301" ||
      code == "E-MOD-1302" || code == "E-MOD-1304" ||
      code == "E-MOD-1307" || code == "E-MOD-1308" ||
      code == "E-MOD-1309" || code == "E-MOD-1310") {
    RecordDiagnosticObligation(
        diag, "diagnostics.NameResolutionAndReservedNames", payload);
  }
  if (code == "E-TYP-1505" || code == "E-MOD-2402" ||
      code == "E-MOD-2433") {
    RecordDiagnosticObligation(diag, "diagnostics.StaticDeclarations",
                               payload);
  }
  if (code == "E-TYP-1506" || code == "E-TYP-1520" ||
      code == "E-TYP-1521" || code == "E-TYP-1530" ||
      code == "E-TYP-1531") {
    RecordDiagnosticObligation(diag, "diagnostics.CoreTypeDiagnostics",
                               payload);
  }
  if (code == "E-TYP-1531") {
    RecordDiagnosticObligation(diag, "diag.16.LiteralAndNameExpressions",
                               payload);
  }
  if (IsChapter15ProcedureContractEntryDiagnostic(code)) {
    RecordDiagnosticObligation(
        diag, "diag.15.ProcedureContractEntryDiagnosticsOwnership", payload);
    RecordDiagnosticObligation(
        diag, "diag-table.15.ProcedureContractEntryDiagnostics", payload);
  }
  if (code == "E-TYP-1507" || code == "E-TYP-1508") {
    RecordDiagnosticObligation(diag, "diag.15.ProcedureDeclarations", payload);
  }
  if (code == "E-TYP-1912") {
    RecordDiagnosticObligation(diag, "diag.15.MethodsAndReceivers", payload);
  }
  if (code == "E-SEM-2536") {
    RecordDiagnosticObligation(diag, "diag.15.MethodLookupDiagnostics",
                               payload);
  }
  if (code == "E-CON-0410" || code == "E-SEM-2808") {
    RecordDiagnosticObligation(diag, "diag.15.ContractClauses", payload);
  }
  if (code == "E-SEM-2801") {
    RecordDiagnosticObligation(diag, "diag.15.Preconditions", payload);
    RecordDiagnosticObligation(diag, "diag.15.VerificationLogic", payload);
  }
  if (code == "E-SEM-2806" || code == "E-SEM-2807") {
    RecordDiagnosticObligation(diag, "diag.15.Postconditions", payload);
  }
  if (code == "E-SEM-2803" || code == "E-SEM-2804") {
    RecordDiagnosticObligation(diag, "diag.15.BehavioralSubtyping", payload);
  }
  if (code == "E-SEM-2820" || code == "E-SEM-2821" ||
      code == "E-SEM-2822" || code == "E-SEM-2823" ||
      code == "E-SEM-2824" || code == "E-SEM-2830" ||
      code == "E-SEM-2831") {
    RecordDiagnosticObligation(diag, "diag.15.Invariants", payload);
  }
  if (IsChapter14RefinementTypeDiagnostic(code)) {
    RecordDiagnosticObligation(diag, "diag.14.RefinementTypes", payload);
  }
  if (code == "E-TYP-2621" || code == "E-TYP-2622") {
    RecordDiagnosticObligation(diag, "diag.14.FoundationalClasses", payload);
  }
  if (code == "E-TYP-2500" || code == "E-TYP-2505") {
    RecordDiagnosticObligation(diag, "diag.Classes", payload);
  }
  if (code == "E-TYP-2401" || code == "E-TYP-2402" ||
      code == "E-TYP-2404" || code == "E-TYP-2501" ||
      code == "E-TYP-2502" || code == "E-TYP-2503" ||
      code == "E-TYP-2506" || code == "E-TYP-2507" ||
      code == "E-UNS-0105") {
    RecordDiagnosticObligation(diag, "diag.14.Implementations", payload);
  }
  if (code == "E-TYP-2504") {
    RecordDiagnosticObligation(diag, "diag.14.AssociatedTypes", payload);
  }
  if (code == "E-TYP-2541") {
    RecordDiagnosticObligation(diag, "diag.14.DynamicClassObjects", payload);
  }
  if (code == "E-TYP-2510" || code == "E-TYP-2511" ||
      code == "E-TYP-2512") {
    RecordDiagnosticObligation(diag, "diag.14.OpaqueTypes", payload);
  }
  if (IsChapter14RefinementPolymorphismDiagnostic(code)) {
    RecordDiagnosticObligation(
        diag, "diag.14.RefinementPolymorphismDiagnosticsOwnership", payload);
    RecordDiagnosticObligation(
        diag, "diag-table.14.RefinementPolymorphismDiagnostics", payload);
  }
  if (code == "E-TYP-1601" || code == "E-TYP-1602" ||
      code == "E-TYP-1603" || code == "E-TYP-1604" ||
      code == "E-TYP-1605") {
    RecordDiagnosticObligation(
        diag, "diagnostics.PermissionAdmissibility", payload);
  }
  if (code == "E-MEM-3021") {
    RecordDiagnosticObligation(diag, "diag.16.EffectfulCoreExpressions",
                               payload);
  }
  if (code == "E-MEM-1206" || code == "E-MEM-1207" ||
      code == "E-MEM-1208" || code == "E-MEM-3001" ||
      code == "E-MEM-3003" || code == "E-MEM-3004" ||
      code == "E-MEM-3005" || code == "E-MEM-3006" ||
      code == "E-MEM-3007" || code == "E-MEM-3020" ||
      code == "E-MEM-3021" || code == "E-MEM-3030") {
    RecordDiagnosticObligation(
        diag, "diagnostics.RuntimeStateAndMemoryDiagnostics", payload);
  }
  if (code == "E-SEM-2525" || code == "E-SEM-2526" ||
      code == "E-SEM-2527" || code == "E-SEM-2528" ||
      code == "E-SEM-2531" || code == "E-SEM-2532" ||
      code == "E-SEM-2533" || code == "E-SEM-2534" ||
      code == "E-SEM-2535" || code == "E-SEM-2536" ||
      code == "E-SEM-2538" || code == "E-SEM-2539" ||
      code == "E-SEM-2591" || code == "E-MEM-3031" ||
      code == "E-UNS-0102" || code == "E-UNS-0103" ||
      code == "E-UNS-0104" || code == "E-UNS-0107" ||
      code == "W-SAF-0100") {
    RecordDiagnosticObligation(
        diag, "diag.16.ExpressionDiagnosticsSupplement", payload);
  }
  if (code == "E-SEM-2525") {
    RecordDiagnosticObligation(diag, "diag.16.OperatorExpressions", payload);
  }
  if (code == "E-SEM-2531" || code == "E-SEM-2532" ||
      code == "E-SEM-2533" || code == "E-SEM-2534" ||
      code == "E-SEM-2535" || code == "E-TYP-1603" ||
      code == "E-TYP-2105" || code == "E-TYP-2106") {
    RecordDiagnosticObligation(diag, "diag.16.CallExpressions", payload);
  }
  if (code == "E-TYP-2106" || code == "E-TYP-2306") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.ExternProcedureDiagnostics", payload);
  }
  if (code == "E-SYS-3352") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.FfiDiagnosticsSupplement", payload);
  }
  if (code == "E-TYP-2632" || code == "E-TYP-2633" ||
      code == "E-TYP-2634" || code == "E-TYP-2635" ||
      code == "E-TYP-2636") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.HostedExportDiagnostics", payload);
  }
  if (code == "E-SEM-2524" || code == "E-SEM-2527" ||
      code == "E-TYP-1801" || code == "E-TYP-1802" ||
      code == "E-TYP-1803" || code == "E-TYP-2202" ||
      code == "E-UNS-0102" || code == "E-UNS-0103" ||
      code == "E-UNS-0107") {
    RecordDiagnosticObligation(
        diag, "diag.16.AccessAndPlaceExpressions", payload);
  }
  if (code == "E-TYP-1801" || code == "E-TYP-1802" ||
      code == "E-TYP-1803" || code == "E-SEM-2524") {
    RecordDiagnosticObligation(diag, "diagnostics.Tuples", payload);
  }
  if (code == "E-TYP-1810" || code == "E-TYP-1812" ||
      code == "E-TYP-1820" || code == "E-TYP-1821" ||
      code == "E-TYP-2201" || code == "E-TYP-2202") {
    RecordDiagnosticObligation(
        diag, "diagnostics.DataTypesSupplement", payload);
  }
  if (code == "E-TYP-2050" || code == "E-TYP-2051" ||
      code == "E-TYP-2052" || code == "E-TYP-2053" ||
      code == "E-TYP-2054" || code == "E-TYP-2055" ||
      code == "E-TYP-2056" || code == "E-TYP-2057" ||
      code == "E-TYP-2058" || code == "E-TYP-2059" ||
      code == "E-TYP-2060" || code == "E-TYP-2061" ||
      code == "E-TYP-2062" || code == "E-TYP-2063" ||
      code == "E-TYP-2064" || code == "E-TYP-2065" ||
      code == "E-TYP-2070" || code == "E-TYP-2071" ||
      code == "E-TYP-2072" || code == "E-TYP-2073" ||
      code == "W-SYS-4010" || code == "E-TYP-2101" ||
      code == "E-TYP-2102" || code == "E-TYP-2103" ||
      code == "E-TYP-2104") {
    RecordDiagnosticObligation(
        diag, "diagnostics.ModalPointerSupplement", payload);
  }
  if (code == "E-TYP-2070" || code == "E-TYP-2071" ||
      code == "E-TYP-2072" || code == "W-SYS-4010") {
    RecordDiagnosticObligation(diag, "diag.ModalWidening", payload);
  }
  if (code == "E-CON-0120" || code == "E-CON-0121" ||
      code == "E-MEM-3006" || code == "E-MEM-3001" ||
      code == "E-SEM-2538" || code == "E-SEM-2539" ||
      code == "E-SEM-2591") {
    RecordDiagnosticObligation(
        diag, "diag.16.ClosureAndPipelineExpressions", payload);
  }
  if (code == "E-CON-0002" || code == "E-CON-0003" ||
      code == "E-CON-0030" || code == "E-CON-0033" ||
      code == "E-CON-0034" || code == "E-CON-0083") {
    RecordDiagnosticObligation(diag, "diagnostics.19.KeyPaths", payload);
  }
  if (code == "E-CON-0001" || code == "E-CON-0004" ||
      code == "E-CON-0006" || code == "E-CON-0031" ||
      code == "E-CON-0032" || code == "E-CON-0070" ||
      code == "E-CON-0085" || code == "E-CON-0086" ||
      code == "W-CON-0001" || code == "W-CON-0002" ||
      code == "W-CON-0003" || code == "W-CON-0009") {
    RecordDiagnosticObligation(
        diag, "diagnostics.19.KeyAcquisitionBlocks", payload);
  }
  if (code == "E-CON-0005" || code == "E-CON-0010" ||
      code == "E-CON-0014" || code == "E-CON-0060" ||
      code == "W-CON-0004" || code == "W-CON-0006" ||
      code == "W-CON-0013") {
    RecordDiagnosticObligation(
        diag, "diagnostics.19.ConflictDetection", payload);
  }
  if (code == "E-CON-0012" || code == "E-CON-0018" ||
      code == "W-CON-0005" || code == "W-CON-0010" ||
      code == "W-CON-0011") {
    RecordDiagnosticObligation(diag, "diagnostics.19.NestedRelease", payload);
  }
  if (code == "E-CON-0090" || code == "E-CON-0091" ||
      code == "E-CON-0092" || code == "E-CON-0093" ||
      code == "E-CON-0094" || code == "E-CON-0095" ||
      code == "E-CON-0096" || code == "E-CON-0097" ||
      code == "W-CON-0020" || code == "W-CON-0021") {
    RecordDiagnosticObligation(
        diag, "diagnostics.19.SpeculativeExecution", payload);
  }
  if (code == "E-CON-0096") {
    RecordDiagnosticObligation(diag, "diagnostics.19.MemoryOrdering", payload);
  }
  if (code == "E-CON-0020" || code == "I-CON-0011" ||
      code == "I-CON-0013") {
    RecordDiagnosticObligation(
        diag, "diagnostics.19.DynamicKeyVerification", payload);
  }
  if (code == "E-CON-0101" || code == "E-CON-0102" ||
      code == "E-CON-0103") {
    RecordDiagnosticObligation(diag, "diagnostics.20.ParallelBlocks",
                               payload);
  }
  if (code == "E-CON-0150" || code == "E-CON-0154" ||
      code == "E-CON-0155" || code == "E-CON-0156" ||
      code == "E-CON-0157" || code == "E-CON-0158" ||
      code == "E-CON-0159" || code == "E-TYP-2640" ||
      code == "E-TYP-2641" || code == "E-TYP-2642") {
    RecordDiagnosticObligation(diag, "diagnostics.20.ExecutionDomains",
                               payload);
  }
  if (code == "E-CON-0120" || code == "E-CON-0121" ||
      code == "E-CON-0122" || code == "E-CON-0131" ||
      code == "E-CON-0151" || code == "E-CON-0153") {
    RecordDiagnosticObligation(diag, "diagnostics.20.CaptureSemantics",
                               payload);
  }
  if (code == "E-CON-0130") {
    RecordDiagnosticObligation(diag, "diagnostics.20.Spawn", payload);
  }
  if (code == "E-CON-0140" || code == "E-CON-0141" ||
      code == "E-CON-0142" || code == "E-CON-0143" ||
      code == "W-CON-0140") {
    RecordDiagnosticObligation(diag, "diagnostics.20.Dispatch", payload);
  }
  if (code == "E-CON-0152") {
    RecordDiagnosticObligation(diag,
                               "diagnostics.20.DeterminismAndNesting",
                               payload);
  }
  if (code == "P-SEM-2862") {
    RecordDiagnosticObligation(
        diag, "diagnostics.20.StructuredParallelismSupplement", payload);
  }
  if (code == "E-TYP-1902" || code == "E-TYP-1903" ||
      code == "E-TYP-1907" || code == "E-TYP-1911") {
    RecordDiagnosticObligation(
        diag, "diag.16.ConstructionExpressions", payload);
  }
  if (code == "E-SEM-2528" || code == "E-MEM-3030" ||
      code == "E-MEM-3031" || code == "E-UNS-0104" ||
      code == "W-SAF-0100") {
    RecordDiagnosticObligation(
        diag, "diag.16.CastAndTransmuteExpressions", payload);
  }
  if (code == "E-CON-0201") {
    RecordDiagnosticObligation(diag, "diagnostics.21.AsyncType", payload);
  }
  if (code == "E-SEM-2713") {
    RecordDiagnosticObligation(diag, "diag.17.BasicPatterns", payload);
  }
  if (code == "E-TYP-1803" || code == "E-SEM-2731") {
    RecordDiagnosticObligation(diag, "diag.17.TupleRecordPatterns", payload);
  }
  if (code == "E-SEM-2741") {
    RecordDiagnosticObligation(diag, "diag.17.EnumModalPatterns", payload);
  }
  if (code == "E-SEM-2721" || code == "E-SEM-2722") {
    RecordDiagnosticObligation(diag, "diag.17.RangePatterns", payload);
  }
  if (code == "E-SEM-2751") {
    RecordDiagnosticObligation(diag, "diag.17.CaseClauses", payload);
  }
  if (code == "E-SEM-2705" || code == "E-TYP-2060" ||
      code == "E-SEM-2741" || code == "E-SEM-2751") {
    RecordDiagnosticObligation(
        diag, "diag.17.ExhaustivenessAndReachability", payload);
  }
  if (code == "E-SEM-2705" || code == "E-SEM-2711" ||
      code == "E-SEM-2713" || code == "E-SEM-2721" ||
      code == "E-SEM-2722" || code == "E-SEM-2731" ||
      code == "E-SEM-2741" || code == "E-SEM-2751" ||
      code == "E-SEM-2761" || code == "E-SEM-2762") {
    RecordDiagnosticObligation(diag, "diag.17.PatternDiagnosticsSupplement",
                               payload);
  }
  if (code == "E-CON-0132" || code == "E-CON-0210" ||
      code == "E-CON-0211" || code == "E-CON-0220" ||
      code == "E-CON-0221" || code == "E-CON-0222" ||
      code == "E-CON-0225") {
    RecordDiagnosticObligation(diag, "diagnostics.21.SuspensionForms",
                               payload);
  }
  if (code == "E-CON-0212" || code == "E-CON-0223" ||
      code == "E-CON-0240" || code == "E-CON-0250" ||
      code == "E-CON-0251" || code == "E-CON-0252" ||
      code == "E-CON-0260" || code == "E-CON-0261" ||
      code == "E-CON-0262" || code == "E-CON-0263" ||
      code == "E-CON-0270" || code == "E-CON-0271") {
    RecordDiagnosticObligation(
        diag, "diagnostics.21.AsyncCompositionDiagnostics", payload);
  }
  if (code == "E-CON-0203" || code == "E-CON-0230") {
    RecordDiagnosticObligation(
        diag, "diagnostics.21.AsyncDiagnosticsSupplement", payload);
  }
  if (code == "E-CON-0133" || code == "E-CON-0213" ||
      code == "E-CON-0224") {
    RecordDiagnosticObligation(
        diag, "diagnostics.21.AsyncKeyDiagnostics", payload);
  }
  if (code == "W-CON-0201" || code == "E-CON-0280" ||
      code == "E-CON-0281") {
    RecordDiagnosticObligation(
        diag, "diagnostics.21.AsyncStateMachineDiagnostics", payload);
  }
  if (code == "W-CTE-0071") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.CompileTimeDiagnosticsSupplement", payload);
  }
  if (code == "E-CTE-0081") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.CompileTimeFormsDiagnosticsReference", payload);
  }
  if (code == "E-CTE-0050" || code == "E-CTE-0051" ||
      code == "E-CTE-0052" || code == "E-CTE-0053") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.ReflectionDiagnosticsReference", payload);
  }
  if (code == "E-CTE-0221") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.QuoteSpliceEmissionDiagnosticsReference",
        payload);
  }
  if (code == "E-CTE-0251") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference",
        payload);
  }
  if (code == "E-SYS-3356") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.BoundaryUnwindingDiagnosticOwnership",
        payload);
  }
  if (code == "E-SYS-3340" || code == "E-SYS-3341" ||
      code == "E-SYS-3342" || code == "E-SYS-3345" ||
      code == "E-SYS-3346" || code == "E-SYS-3347" ||
      code == "E-SYS-3350" || code == "E-SYS-3351" ||
      code == "E-SYS-3355" || code == "E-SYS-3356" ||
      code == "E-SYS-3357" || code == "E-SYS-3358" ||
      code == "E-FFI-0350" || code == "W-SYS-3350" ||
      code == "W-SYS-3355") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.FfiAttributeDiagnostics", payload);
  }
  if (code == "E-TYP-2623" || code == "E-TYP-2624" ||
      code == "E-TYP-2625" || code == "E-TYP-2626" ||
      code == "E-TYP-2627" || code == "E-TYP-2628" ||
      code == "E-TYP-2629" || code == "E-TYP-2630") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.FfiSafeDiagnostics", payload);
  }
  if (code == "E-SYS-3360") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.CapabilityIsolationDiagnostics", payload);
  }
  if (code == "E-SEM-2850" || code == "E-SEM-2851" ||
      code == "E-SEM-2852" || code == "E-SEM-2853" ||
      code == "E-SEM-2854" || code == "E-SEM-2855" ||
      code == "E-SEM-2856" || code == "P-SEM-2860" ||
      code == "P-SEM-2861") {
    RecordDiagnosticObligation(
        diag, "diagnostics.23.ForeignContractDiagnostics", payload);
  }
  if (code == "E-CTE-0311") {
    RecordDiagnosticObligation(
        diag, "diagnostics.22.DeriveTargetsDiagnosticsReference", payload);
  }
}

// In-place emission - O(1) amortized, preferred for performance
void Emit(DiagnosticStream& stream, const Diagnostic& diag) {
  SPEC_RULE("Emit-Append");
  SpecDefsDiagnosticTypes();
  if (Conformance::Enabled()) {
    const std::string payload = DiagPayload(diag);
    for (const auto& obligation_id : diag.obligation_ids) {
      RecordDiagnosticObligation(diag, obligation_id, payload);
    }
    RecordDiagnosticTableObligations(diag, payload);
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
