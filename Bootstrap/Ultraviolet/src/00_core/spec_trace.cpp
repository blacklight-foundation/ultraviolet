// =============================================================================
// MIGRATION MAPPING: spec_trace.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 0.3.2 "Observable Behavior Equivalence for Bootstrap" (lines 181-207)
//     - DiagObs(d) = <d.code, d.severity, d.message, d.span>
//     - DiagStream(C, P) = [DiagObs(d_1), ..., DiagObs(d_k)]
//     - ObsComp(C, P) for compiler observable behavior
//   - Section 0.3.1 "Bootstrap Milestones" (lines 155-179)
//     - Bootstrap equivalence verification requirements
//   - General: Conformance tracing is implementation infrastructure, not
//     directly specified but supports verification of spec rules
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/spec_trace.cpp
//   - Lines 1-134 (entire file)
//
// CONTENT TO MIGRATE:
//   - TraceState struct (lines 13-20) [internal]
//     Thread-safe state: mutex, output stream, domain, phase, root, enabled
//   - State() -> TraceState& (lines 22-25) [internal]
//     Singleton accessor for global trace state
//   - EncodePayload(payload) -> string (lines 27-53) [internal]
//     URL-encodes special characters: \t->%09, \n->%0A, %->%25, ;->%3B, =->%3D
//   - RelPath(path, root) -> string (lines 55-65) [internal]
//     Computes relative path for trace output
//   - Conformance::Init(path, domain) (lines 69-84)
//     Initializes trace output file, writes header "compile_conformance_v1\n"
//   - Conformance::SetRoot(root) (lines 86-90)
//     Sets project root for relative path computation
//   - Conformance::SetPhase(phase) (lines 92-96)
//     Sets current compilation phase for trace context
//   - Conformance::Record(rule_id, span, payload) (lines 98-124)
//     Records trace entry: domain, phase, rule_id, file, line:col range, payload
//     Format: tab-separated fields, URL-encoded payload
//   - Conformance::Record(rule_id) (lines 126-128)
//     Convenience overload with no span or payload
//   - Conformance::Enabled() -> bool (lines 130-132)
//     Returns whether tracing is active
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/spec_trace.h (header)
//     - Conformance class with static methods
//   - ultraviolet/include/00_core/span.h
//     - Span struct (optional span parameter)
//   - ultraviolet/include/00_core/path.h
//     - Normalize(), Relative() for path handling
//   - <fstream> for file output
//   - <mutex> for thread safety
//   - <sstream> for string building
//
// REFACTORING NOTES:
//   1. Thread-safe via mutex lock on all operations
//   2. Output format: tab-separated, one record per line
//     domain \t phase \t rule_id \t file \t start_line \t start_col \t end_line \t end_col \t payload
//   3. File header "compile_conformance_v1\n" for format versioning
//   4. Phase defaults to "-" if not set
//   5. File defaults to "-" if span is nullopt
//   6. Payload encoding prevents field delimiter conflicts
//   7. This is infrastructure code - not directly from spec but supports
//      conformance tracing via SPEC_DEF and SPEC_RULE macros
//   8. Consider buffering for performance if trace volume is high
//
// =============================================================================

#include "00_core/spec_trace.h"
#include "00_core/host/services.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include "00_core/path.h"

namespace ultraviolet::core {

namespace {

struct TraceState {
  std::mutex mutex;
  std::ofstream out;
  std::array<char, 1 << 20> out_buffer{};
  std::string domain;
  std::string phase;
  std::string root;
  std::atomic<bool> enabled{false};
};

TraceState& State() {
  static TraceState state;
  return state;
}

std::string EncodePayload(std::string_view payload) {
  // Escape bytes that can break tab-separated rows or key/value parsing.
  std::string out;
  out.reserve(payload.size() + 8);
  for (char c : payload) {
    switch (c) {
      case '\t':
        out += "%09";
        break;
      case '\n':
        out += "%0A";
        break;
      case '\r':
        out += "%0D";
        break;
      case '%':
        out += "%25";
        break;
      case ';':
        out += "%3B";
        break;
      case '=':
        out += "%3D";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

unsigned long CurrentProcessId() {
  return CurrentHostProcessId();
}

std::uint64_t CurrentThreadId() {
  return CurrentHostThreadId();
}

std::uint64_t TimestampMs() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string RelPath(std::string_view path, std::string_view root) {
  const std::string norm = Normalize(path);
  if (root.empty()) {
    return norm.empty() ? "-" : norm;
  }
  const auto rel = Relative(norm, root);
  if (!rel.has_value() || rel->empty()) {
    return norm.empty() ? "-" : norm;
  }
  return *rel;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

std::optional<std::string_view> PayloadField(std::string_view payload,
                                             std::string_view key) {
  std::size_t pos = 0;
  while (pos <= payload.size()) {
    std::size_t end = payload.find(';', pos);
    if (end == std::string_view::npos) {
      end = payload.size();
    }
    const std::string_view segment = payload.substr(pos, end - pos);
    const std::size_t eq = segment.find('=');
    if (eq != std::string_view::npos && segment.substr(0, eq) == key) {
      return segment.substr(eq + 1);
    }
    if (end == payload.size()) {
      break;
    }
    pos = end + 1;
  }
  return std::nullopt;
}

std::string_view DefaultCategoryForRule(std::string_view rule_id) {
  if (StartsWith(rule_id, "Log-")) {
    return "log";
  }
  if (StartsWith(rule_id, "Diag-")) {
    return "diagnostic";
  }
  return "runtime";
}

std::string_view DefaultLevelForRule(std::string_view rule_id) {
  if (StartsWith(rule_id, "Log-")) {
    return "info";
  }
  if (StartsWith(rule_id, "Diag-")) {
    return "warning";
  }
  if (StartsWith(rule_id, "Panic") || StartsWith(rule_id, "RuntimePanic") ||
      StartsWith(rule_id, "PanicCheck")) {
    return "error";
  }
  return "trace";
}

}  // namespace

void Conformance::Init(const std::string& path, std::string_view domain) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.enabled.store(false, std::memory_order_relaxed);
  state.out.close();
  state.out.clear();
  state.out.rdbuf()->pubsetbuf(state.out_buffer.data(),
                               static_cast<std::streamsize>(
                                   state.out_buffer.size()));
  state.out.open(path, std::ios::binary | std::ios::trunc);
  if (!state.out) {
    return;
  }
  state.domain = std::string(domain);
  state.phase = "";
  state.enabled.store(true, std::memory_order_relaxed);
  state.out << "compile_conformance_v1\n";
}

void Conformance::SetRoot(const std::string& root) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.root = Normalize(root);
}

void Conformance::SetPhase(std::string_view phase) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.phase = std::string(phase);
}

void Conformance::Record(std::string_view rule_id,
                       const std::optional<Span>& span,
                       std::string_view payload) {
  auto& state = State();
  if (!state.enabled.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.enabled.load(std::memory_order_relaxed)) {
    return;
  }
  const std::string file =
      span.has_value() ? RelPath(span->file, state.root) : "-";
  const std::size_t start_line = span.has_value() ? span->start_line : 0;
  const std::size_t start_col = span.has_value() ? span->start_col : 0;
  const std::size_t end_line = span.has_value() ? span->end_line : 0;
  const std::size_t end_col = span.has_value() ? span->end_col : 0;
  std::string payload_with_meta;
  payload_with_meta.reserve(payload.size() + 128);
  payload_with_meta += "ts_ms=";
  payload_with_meta += std::to_string(TimestampMs());
  payload_with_meta += ";pid=";
  payload_with_meta += std::to_string(CurrentProcessId());
  payload_with_meta += ";tid=";
  payload_with_meta += std::to_string(CurrentThreadId());
  if (!PayloadField(payload, "level").has_value()) {
    payload_with_meta += ";level=";
    payload_with_meta += DefaultLevelForRule(rule_id);
  }
  if (!PayloadField(payload, "category").has_value()) {
    payload_with_meta += ";category=";
    payload_with_meta += DefaultCategoryForRule(rule_id);
  }
  if (!payload.empty()) {
    payload_with_meta += ";";
    payload_with_meta += std::string(payload);
  }
  const std::string encoded = EncodePayload(payload_with_meta);

  state.out << state.domain << '\t'
            << (state.phase.empty() ? "-" : state.phase) << '\t'
            << rule_id << '\t'
            << file << '\t';
  if (span.has_value() || start_line != 0 || start_col != 0 ||
      end_line != 0 || end_col != 0) {
    state.out << start_line << '\t'
              << start_col << '\t'
              << end_line << '\t'
              << end_col << '\t';
  } else {
    state.out << "-\t-\t-\t-\t";
  }
  state.out << encoded << '\n';
}

void Conformance::Record(std::string_view rule_id) {
  Record(rule_id, std::nullopt, "");
}

bool Conformance::Enabled() {
  return State().enabled.load(std::memory_order_relaxed);
}

}  // namespace ultraviolet::core

