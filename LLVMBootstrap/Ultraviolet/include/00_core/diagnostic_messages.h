#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostic_render.h"
#include "00_core/diagnostics.h"
#include "00_core/span.h"

namespace ultraviolet::core {

// Message locale for future i18n support.
// Currently only EnUS is supported; locale parameter is accepted but ignored.
enum class MessageLocale {
  EnUS,  // English (US) - currently the only supported locale
};

struct MessageArg {
  std::string_view key;
  std::string_view value;
};

std::optional<std::string_view> MessageForCode(
    std::string_view code,
    MessageLocale locale = MessageLocale::EnUS);

std::optional<Severity> SeverityForCode(
    std::string_view code,
    MessageLocale locale = MessageLocale::EnUS);

std::string FormatMessage(std::string_view message_template,
                          const std::vector<MessageArg>& args);

std::optional<Diagnostic> MakeDiagnostic(
    std::string_view code,
    std::optional<Span> span = std::nullopt,
    MessageLocale locale = MessageLocale::EnUS);

std::optional<Diagnostic> MakeDiagnosticById(
    std::string_view diag_id,
    std::optional<Span> span = std::nullopt,
    MessageLocale locale = MessageLocale::EnUS,
    DiagnosticOrigin origin = DiagnosticOrigin::Internal);

std::optional<Diagnostic> MakeExternalDiagnostic(
    std::string_view code,
    MessageLocale locale = MessageLocale::EnUS);

void EmitExternalDiagnostic(DiagnosticStream& stream,
                            std::string_view code,
                            MessageLocale locale = MessageLocale::EnUS);

void EmitDiagnosticById(DiagnosticStream& stream,
                        std::string_view diag_id,
                        std::optional<Span> span = std::nullopt,
                        MessageLocale locale = MessageLocale::EnUS,
                        DiagnosticOrigin origin = DiagnosticOrigin::Internal);

}  // namespace ultraviolet::core
