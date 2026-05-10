#include "00_core/diagnostic_messages.h"

#include <algorithm>
#include <string>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_codes.h"
#include "00_core/diagnostic_render.h"

namespace cursive::core {

namespace {

#include "generated/diag_registry.inc"

static void SpecDefsDiagMessages() {
  SPEC_DEF("DiagId-Code-Map", "2.4");
  SPEC_DEF("SeverityColumn", "2.3");
  SPEC_DEF("ConditionColumn", "2.3");
  SPEC_DEF("Message", "2.3");
}

static std::optional<Severity> ParseSeverity(std::string_view severity) {
  if (severity == "Error") {
    return Severity::Error;
  }
  if (severity == "Warning") {
    return Severity::Warning;
  }
  if (severity == "Info") {
    return Severity::Info;
  }
  if (severity == "Panic") {
    return Severity::Panic;
  }
  if (severity == "Note") {
    return Severity::Note;
  }
  return std::nullopt;
}

static const DiagRegistryRow* FindEntry(std::string_view code) {
  const auto begin = std::begin(kDiagRegistryRows);
  const auto end = std::end(kDiagRegistryRows);
  auto it = std::lower_bound(
      begin,
      end,
      code,
      [](const DiagRegistryRow& entry, std::string_view value) {
        return std::string_view(entry.code) < value;
      });
  if (it == end) {
    return nullptr;
  }
  if (code == std::string_view(it->code)) {
    return &(*it);
  }
  return nullptr;
}

}  // namespace

std::optional<std::string_view> MessageForCode(
    std::string_view code, [[maybe_unused]] MessageLocale locale) {
  SpecDefsDiagMessages();
  // locale reserved for future i18n support
  const auto* entry = FindEntry(code);
  if (!entry) {
    return std::nullopt;
  }
  return std::string_view(entry->condition);
}

std::optional<Severity> SeverityForCode(
    std::string_view code, [[maybe_unused]] MessageLocale locale) {
  SpecDefsDiagMessages();
  // locale reserved for future i18n support
  const auto* entry = FindEntry(code);
  if (!entry) {
    return std::nullopt;
  }
  return ParseSeverity(entry->severity);
}

std::string FormatMessage(std::string_view message_template,
                          const std::vector<MessageArg>& args) {
  if (message_template.find('{') == std::string_view::npos) {
    return std::string(message_template);
  }

  std::string out;
  out.reserve(message_template.size());

  size_t i = 0;
  while (i < message_template.size()) {
    if (message_template[i] != '{') {
      out.push_back(message_template[i]);
      ++i;
      continue;
    }

    const size_t close = message_template.find('}', i + 1);
    if (close == std::string_view::npos) {
      out.append(message_template.substr(i));
      break;
    }

    const std::string_view key =
        message_template.substr(i + 1, close - i - 1);
    const auto it = std::find_if(args.begin(), args.end(),
                                 [key](const MessageArg& arg) {
                                   return arg.key == key;
                                 });
    if (it == args.end()) {
      out.append(message_template.substr(i, close - i + 1));
    } else {
      out.append(it->value);
    }
    i = close + 1;
  }

  return out;
}

std::optional<Diagnostic> MakeDiagnostic(
    std::string_view code, std::optional<Span> span,
    [[maybe_unused]] MessageLocale locale) {
  SpecDefsDiagMessages();
  // locale reserved for future i18n support
  const auto* entry = FindEntry(code);
  if (!entry) {
    return std::nullopt;
  }
  const auto severity = ParseSeverity(entry->severity);
  if (!severity.has_value()) {
    return std::nullopt;
  }
  Diagnostic diag;
  diag.code = std::string(entry->code);
  diag.severity = *severity;
  diag.message = entry->condition;
  diag.span = span;
  return diag;
}

std::optional<Diagnostic> MakeDiagnosticById(
    std::string_view diag_id, std::optional<Span> span, MessageLocale locale,
    DiagnosticOrigin origin) {
  const auto resolved = ResolveDiagCode(std::string(diag_id));
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  auto diag = MakeDiagnostic(*resolved, span, locale);
  if (!diag.has_value()) {
    return std::nullopt;
  }
  return ApplyNoSpanExternal(*diag, origin);
}

std::optional<Diagnostic> MakeExternalDiagnostic(
    std::string_view code, MessageLocale locale) {
  return MakeDiagnosticById(code, std::nullopt, locale,
                            DiagnosticOrigin::External);
}

void EmitExternalDiagnostic(DiagnosticStream& stream,
                            std::string_view code,
                            MessageLocale locale) {
  if (auto diag = MakeDiagnosticById(code, std::nullopt, locale,
                                     DiagnosticOrigin::External)) {
    Emit(stream, *diag);
  }
}

void EmitDiagnosticById(DiagnosticStream& stream,
                        std::string_view diag_id,
                        std::optional<Span> span,
                        MessageLocale locale,
                        DiagnosticOrigin origin) {
  if (auto diag = MakeDiagnosticById(diag_id, span, locale, origin)) {
    Emit(stream, *diag);
  }
}
}  // namespace cursive::core
