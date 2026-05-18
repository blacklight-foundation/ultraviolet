#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifndef UV_TEST_COMPILER_PATH
#error "UV_TEST_COMPILER_PATH must be defined"
#endif

#ifndef UV_TEST_RUNTIME_LIB_PATH
#error "UV_TEST_RUNTIME_LIB_PATH must be defined"
#endif

#ifndef UV_TEST_TARGET_PROFILE
#error "UV_TEST_TARGET_PROFILE must be defined"
#endif

#ifndef UV_TEST_EXECUTABLE_SUFFIX
#error "UV_TEST_EXECUTABLE_SUFFIX must be defined"
#endif

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

#ifndef UV_TEST_LLD_LINK_PATH
#error "UV_TEST_LLD_LINK_PATH must be defined"
#endif

#ifndef UV_TEST_LLVM_LIB_PATH
#error "UV_TEST_LLVM_LIB_PATH must be defined"
#endif

#ifndef UV_TEST_LLVM_AS_PATH
#error "UV_TEST_LLVM_AS_PATH must be defined"
#endif

namespace {

std::string Quote(std::string_view value) {
#ifdef _WIN32
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') {
      out += "\\\"";
    } else {
      out += c;
    }
  }
  out += "\"";
  return out;
#else
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
#endif
}

bool WriteFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::cerr << "failed to open " << path << " for writing\n";
    return false;
  }
  out << text;
  return out.good();
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "failed to open " << path << " for reading\n";
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::optional<std::string_view> FunctionBody(
    std::string_view module_ir,
    std::string_view signature) {
  std::size_t start = std::string_view::npos;
  for (std::size_t pos = module_ir.find(signature);
       pos != std::string_view::npos;
       pos = module_ir.find(signature, pos + signature.size())) {
    const std::size_t line_start =
        module_ir.rfind('\n', pos) == std::string_view::npos
            ? 0
            : module_ir.rfind('\n', pos) + 1;
    if (module_ir.substr(line_start, 7) == "define ") {
      start = line_start;
      break;
    }
  }
  if (start == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t next = module_ir.find("\ndefine ", start + signature.size());
  const std::size_t end = next == std::string_view::npos ? module_ir.size() : next;
  return module_ir.substr(start, end - start);
}

bool ContainsBefore(std::string_view body,
                    std::string_view needle,
                    std::string_view marker) {
  const std::size_t needle_pos = body.find(needle);
  const std::size_t marker_pos = body.find(marker);
  return needle_pos != std::string_view::npos &&
         marker_pos != std::string_view::npos &&
         needle_pos < marker_pos;
}

bool ContainsAfterLast(std::string_view body,
                       std::string_view needle,
                       std::string_view marker) {
  const std::size_t marker_pos = body.rfind(marker);
  if (marker_pos == std::string_view::npos) {
    return false;
  }
  return body.find(needle, marker_pos + marker.size()) != std::string_view::npos;
}

std::size_t CountOccurrences(std::string_view body, std::string_view needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while (true) {
    pos = body.find(needle, pos);
    if (pos == std::string_view::npos) {
      return count;
    }
    ++count;
    pos += needle.size();
  }
}

bool IsLlvmLabelLine(std::string_view line) {
  if (line.empty() || line.front() == ' ' || line.front() == '\t' ||
      line.front() == '}') {
    return false;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return false;
  }
  const std::string_view label = line.substr(0, colon);
  return label.find(' ') == std::string_view::npos &&
         label.find('\t') == std::string_view::npos;
}

bool CheckFailBlocksDeferToPanicCheck(std::string_view body) {
  bool saw_check_fail = false;
  for (std::size_t pos = body.find("check_fail");
       pos != std::string_view::npos;
       pos = body.find("check_fail", pos + 10)) {
    const std::size_t line_start =
        body.rfind('\n', pos) == std::string_view::npos
            ? 0
            : body.rfind('\n', pos) + 1;
    const std::size_t line_end = body.find('\n', pos);
    const std::string_view label =
        body.substr(line_start,
                    (line_end == std::string_view::npos ? body.size() : line_end) -
                        line_start);
    if (!IsLlvmLabelLine(label) || label.substr(0, 10) != "check_fail") {
      continue;
    }

    saw_check_fail = true;
    bool branches_to_continuation = false;
    std::size_t scan = line_end == std::string_view::npos ? body.size() : line_end + 1;
    while (scan < body.size()) {
      const std::size_t next_end = body.find('\n', scan);
      const std::string_view line =
          body.substr(scan,
                      (next_end == std::string_view::npos ? body.size() : next_end) -
                          scan);
      if (IsLlvmLabelLine(line)) {
        break;
      }
      if (line.find(" ret ") != std::string_view::npos ||
          line.find("\tret ") != std::string_view::npos) {
        return false;
      }
      if (line.find("br label %check_ok") != std::string_view::npos) {
        branches_to_continuation = true;
      }
      if (next_end == std::string_view::npos) {
        break;
      }
      scan = next_end + 1;
    }
    if (!branches_to_continuation) {
      return false;
    }
  }

  return saw_check_fail;
}

bool HasByteBitwiseNot(std::string_view body) {
  return body.find("xor i8") != std::string_view::npos;
}

bool HasAtLeastBoolNots(std::string_view body, std::size_t expected) {
  return CountOccurrences(body, "xor i1") >= expected;
}

int RunCommand(const std::string& command) {
  return std::system(command.c_str());
}

bool InstallTool(const std::filesystem::path& tool_root,
                 const std::filesystem::path& source,
                 std::string_view name) {
  std::error_code ec;
  std::filesystem::create_directories(tool_root, ec);
  if (ec) {
    std::cerr << "failed to create tool directory " << tool_root << ": "
              << ec.message() << "\n";
    return false;
  }
  if (!std::filesystem::exists(source, ec) || ec) {
    std::cerr << "required test tool is missing: " << source << "\n";
    return false;
  }

  const std::filesystem::path target = tool_root / std::filesystem::path(name);
  std::filesystem::remove(target, ec);
  ec.clear();
  std::filesystem::create_symlink(source, target, ec);
  if (!ec) {
    return true;
  }

  ec.clear();
  std::filesystem::copy_file(
      source,
      target,
      std::filesystem::copy_options::overwrite_existing,
      ec);
  if (ec) {
    std::cerr << "failed to install test tool " << source << " as " << target
              << ": " << ec.message() << "\n";
    return false;
  }
  return true;
}

std::string CommandWithToolPath(const std::filesystem::path& tool_root,
                                const std::string& command) {
#ifdef _WIN32
  return "set \"PATH=" + tool_root.generic_string() + ";%PATH%\" && " +
         command;
#else
  return "PATH=" + Quote(tool_root.generic_string()) + ":$PATH " + command;
#endif
}

std::string RunExecutableCommand(const std::filesystem::path& executable,
                                 const std::filesystem::path& run_log) {
#ifdef _WIN32
  return "cd /d " + Quote(executable.parent_path().generic_string()) + " && " +
         Quote(executable.filename().string()) + " > " +
         Quote(run_log.generic_string()) + " 2>&1";
#else
  return Quote(executable.generic_string()) + " > " +
         Quote(run_log.generic_string()) + " 2>&1";
#endif
}

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << UV_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << UV_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"diagnostic_path\"\n";
  out << "kind = \"executable\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/diagnostic_path\"\n";
  out << "emit_ir = \"ll\"\n";
  return out.str();
}

std::string FixtureSource() {
  return std::string{R"uv(public modal DiagnosticSeverity {
    @Error {
        public procedure isError(~) -> bool {
            return true
        }
    }

    @Info {
        public procedure isError(~) -> bool {
            return false
        }
    }
}

public type DiagnosticSeverityValue =
    DiagnosticSeverity@Error | DiagnosticSeverity@Info

public record DiagnosticCode {
    public text: string@View
}

public modal DiagnosticCodeOption {
    @Absent {}

    @Present {
        public code: DiagnosticCode
    }
}

public type DiagnosticCodeOptionValue =
    DiagnosticCodeOption@Absent | DiagnosticCodeOption@Present

public let CODE_CLI_UNKNOWN_COMMAND: string@View = "E-CLI-0001"
public let CODE_CLI_PIPELINE_UNAVAILABLE: string@View = "E-CLI-0002"
public let CODE_CLI_OUTPUT_WRITE_FAILED: string@View = "E-CLI-0003"

public record SourceSpan {
    public file_path: string@View
    public start_offset: usize
    public end_offset: usize
    public start_line: usize
    public start_column: usize
    public end_line: usize
    public end_column: usize
}

public modal DiagnosticSource {
    @Unknown {}

    @Known {
        public span: SourceSpan
    }
}

public type DiagnosticSourceValue =
    DiagnosticSource@Unknown | DiagnosticSource@Known

public record Diagnostic {
    public code: DiagnosticCodeOptionValue
    public severity: DiagnosticSeverityValue
    public message: string@View
    public source: DiagnosticSourceValue

    public procedure isError(~) -> bool {
        return diagnosticSeverityIsError(self.severity)
    }
}

public modal DiagnosticStream {
    @Empty {}

    @Entry {
        public diagnostic: Diagnostic
        public tail: DiagnosticStreamValue

        public procedure currentDiagnostic(~) -> Diagnostic {
            return self.diagnostic
        }

        public procedure remainingDiagnostics(~) -> DiagnosticStreamValue {
            return self.tail
        }
    }
}

public type DiagnosticStreamValue =
    DiagnosticStream@Empty | DiagnosticStream@Entry

public modal CommandFailureReason {
    @UnknownCommand {
        public command_name: string@View
    }

    @PipelineUnavailable {}

    @OutputWriteFailed {}

    @TestTargetRejected {
        public code: string@View
        public message: string@View
    }
}

public type CommandFailureReasonValue =
    CommandFailureReason@OutputWriteFailed |
    CommandFailureReason@PipelineUnavailable |
    CommandFailureReason@TestTargetRejected |
    CommandFailureReason@UnknownCommand

public modal CommandResult {
    @Succeeded {
        public exit_code: i32

        public procedure exitCode(~) -> i32 {
            return self.exit_code
        }
    }

    @Failed {
        public exit_code: i32
        public diagnostics: DiagnosticStreamValue

        public procedure exitCode(~) -> i32 {
            return self.exit_code
        }
    }
}

public type CommandResultValue =
    CommandResult@Failed | CommandResult@Succeeded

public procedure diagnosticCode(text: string@View) -> DiagnosticCode {
    return DiagnosticCode { text: text }
}

public procedure absentDiagnosticCode() -> DiagnosticCodeOptionValue {
    return DiagnosticCodeOption@Absent {}
}

public procedure presentDiagnosticCode(code: DiagnosticCode) -> DiagnosticCodeOptionValue {
    return DiagnosticCodeOption@Present { code: code }
}

public procedure diagnosticCodeFromText(text: string@View) -> DiagnosticCodeOptionValue {
    return presentDiagnosticCode(diagnosticCode(text))
}

public procedure diagnosticSeverityError() -> DiagnosticSeverityValue {
    return DiagnosticSeverity@Error {}
}

public procedure diagnosticSeverityIsError(severity: DiagnosticSeverityValue) -> bool {
    return if severity is {
        @Error {
            severity~>isError()
        }
        @Info {
            severity~>isError()
        }
    }
}

public procedure unknownDiagnosticSource() -> DiagnosticSourceValue {
    return DiagnosticSource@Unknown {}
}

public procedure diagnosticSourceIsUnknown(source: DiagnosticSourceValue) -> bool {
    return if source is {
        @Unknown {
            true
        }
        @Known {
            false
        }
    }
}

public procedure diagnostic(
    code: DiagnosticCodeOptionValue,
    severity: DiagnosticSeverityValue,
    message: string@View,
    source: DiagnosticSourceValue
) -> Diagnostic {
    return Diagnostic {
        code: code,
        severity: severity,
        message: message,
        source: source
    }
}

public procedure errorDiagnostic(
    code: string@View,
    message: string@View
) -> Diagnostic {
    return diagnostic(
        diagnosticCodeFromText(code),
        diagnosticSeverityError(),
        message,
        unknownDiagnosticSource()
    )
}

public procedure emptyDiagnosticStream(
    diagnostics_region: unique Region@Active
) -> DiagnosticStreamValue {
    return diagnostics_region ^ DiagnosticStream@Empty {}
}

public procedure singleDiagnosticStream(
    diagnostics_region: unique Region@Active,
    diagnostic: Diagnostic
) -> DiagnosticStreamValue {
    let empty_tail: DiagnosticStreamValue =
        emptyDiagnosticStream(diagnostics_region)
    return diagnostics_region ^ DiagnosticStream@Entry {
        diagnostic: diagnostic,
        tail: move empty_tail
    }
}

public procedure singleErrorDiagnosticStream(
    diagnostics_region: unique Region@Active,
    code: string@View,
    message: string@View
) -> DiagnosticStreamValue {
    return singleDiagnosticStream(
        diagnostics_region,
        errorDiagnostic(code, message)
    )
}

public procedure diagnosticMessage(diagnostic: Diagnostic) -> string@View {
    return diagnostic.message
}

public procedure diagnosticHasMessage(diagnostic: Diagnostic) -> bool {
    return !diagnosticTextEquals(diagnostic.message, "")
}

public procedure diagnosticCodeMatches(
    diagnostic: Diagnostic,
    expected: string@View
) -> bool {
    return if diagnostic.code is {
        @Absent {
            false
        }
        @Present { code } {
            diagnosticTextEquals(code.text, expected)
        }
    }
}

public procedure unknownCommandFailure(
    command_name: string@View
) -> CommandFailureReasonValue {
    return CommandFailureReason@UnknownCommand {
        command_name: command_name
    }
}

public procedure rejectedTargetFailure(
    code: string@View,
    message: string@View
) -> CommandFailureReasonValue {
    return CommandFailureReason@TestTargetRejected {
        code: code,
        message: message
    }
}

public procedure commandFailureDiagnostics(
    diagnostics_region: unique Region@Active,
    reason: CommandFailureReasonValue
) -> DiagnosticStreamValue {
    return if reason is {
        @UnknownCommand {
            singleErrorDiagnosticStream(
                diagnostics_region,
                CODE_CLI_UNKNOWN_COMMAND,
                "unknown command"
            )
        }
        @PipelineUnavailable {
            singleErrorDiagnosticStream(
                diagnostics_region,
                CODE_CLI_PIPELINE_UNAVAILABLE,
                "compiler pipeline unavailable"
            )
        }
        @OutputWriteFailed {
            singleErrorDiagnosticStream(
                diagnostics_region,
                CODE_CLI_OUTPUT_WRITE_FAILED,
                "failed to write command output"
            )
        }
        @TestTargetRejected { code, message } {
            singleErrorDiagnosticStream(diagnostics_region, code, message)
        }
    }
}

public procedure commandFailed(
    diagnostics_region: unique Region@Active,
    reason: CommandFailureReasonValue
) -> CommandResultValue {
    return commandFailedWithDiagnostics(
        commandFailureDiagnostics(diagnostics_region, reason)
    )
}

public procedure commandFailedWithDiagnostics(
    move diagnostics: DiagnosticStreamValue
) -> CommandResultValue {
    return CommandResult@Failed {
        exit_code: 1,
        diagnostics: move diagnostics
    }
}

public procedure diagnosticTextEquals(left: string@View, right: string@View) -> bool {
    let left_length: usize = string::length(left)
    let right_length: usize = string::length(right)
    if (left_length != right_length) {
        return false
    }

    var index: usize = 0
    loop {
        if (index >= left_length) {
            break
        }

        if (diagnosticTextByte(left, index) != diagnosticTextByte(right, index)) {
            return false
        }

        index = index + 1
    }
    return true
}

public procedure diagnosticTextByte(text: string@View, index: usize) -> u8
|: index < string::length(text)
{
    let view: bytes@View = bytes::view_string(text)
    let data: const [u8] = bytes::as_slice(view)
    return data[index]
}

public procedure diagnosticSeverityText(severity: DiagnosticSeverityValue) -> string@View {
    return if severity is {
        @Error {
            "error"
        }
        @Info {
            "info"
        }
    }
}

)uv"} + R"uv(
public procedure writeStderrPart(io: $IO, text: string@View) -> bool {
    let output: Outcome<(), IoError> = io~>write_stderr(text)
    return if output is {
        @Value {
            true
        }
        @Error {
            false
        }
    }
}

public record DiagnosticDriverHost {
    private _fs: $IO
    private _heap: $HeapAllocator
    private _sys: System
    private _executable_path: string@View
    private _current_directory: string@View

    public procedure executablePath(~) -> string@View {
        return self._executable_path
    }

    public procedure currentDirectory(~) -> string@View {
        return self._current_directory
    }

    public procedure fileSystem(~) -> $IO {
        return self._fs
    }

    public procedure heap(~) -> $HeapAllocator {
        return self._heap
    }

    public procedure system(~) -> System {
        return self._sys
    }

    public procedure writeStderr(~, text: string@View) -> Outcome<(), IoError> {
        return self._fs~>write_stderr(text)
    }

    public procedure writeStderrPart(~, text: string@View) -> bool {
        let output: Outcome<(), IoError> = self~>writeStderr(text)
        return if output is {
            @Value {
                true
            }
            @Error {
                false
            }
        }
    }

    public procedure writeDiagnostic(~, diagnostic: Diagnostic) -> bool {
        if (!self~>writeDiagnosticHead(diagnostic)) {
            return false
        }
        if (diagnosticHasMessage(diagnostic)) {
            if (!self~>writeStderrPart(": ")) {
                return false
            }
            if (!self~>writeStderrPart(diagnosticMessage(diagnostic))) {
                return false
            }
        }
        if (!self~>writeDiagnosticSpan(diagnostic.source)) {
            return false
        }
        return self~>writeStderrPart("\n")
    }

    public procedure writeDiagnosticHead(~, diagnostic: Diagnostic) -> bool {
        return if diagnostic.code is {
            @Absent {
                self~>writeStderrPart(diagnosticSeverityText(diagnostic.severity))
            }
            @Present { code } {
                if (!self~>writeStderrPart(code.text)) {
                    return false
                }
                if (!self~>writeStderrPart(" (")) {
                    return false
                }
                if (!self~>writeStderrPart(diagnosticSeverityText(diagnostic.severity))) {
                    return false
                }
                if (!self~>writeStderrPart(")")) {
                    return false
                }
                true
            }
        }
    }

    public procedure writeDiagnosticSpan(~, diagnostic_source: DiagnosticSourceValue) -> bool {
        return if diagnostic_source is {
            @Unknown {
                true
            }
            @Known { span } {
                if (!self~>writeStderrPart(" @")) {
                    return false
                }
                if (!self~>writeStderrPart(span.file_path)) {
                    return false
                }
                true
            }
        }
    }
}

public procedure writeDiagnosticHead(io: $IO, diagnostic: Diagnostic) -> bool {
    return if diagnostic.code is {
        @Absent {
            writeStderrPart(io, diagnosticSeverityText(diagnostic.severity))
        }
        @Present { code } {
            if (!writeStderrPart(io, code.text)) {
                return false
            }
            if (!writeStderrPart(io, " (")) {
                return false
            }
            if (!writeStderrPart(io, diagnosticSeverityText(diagnostic.severity))) {
                return false
            }
            if (!writeStderrPart(io, ")")) {
                return false
            }
            true
        }
    }
}

public procedure writeFirstDiagnostic(
    host: DiagnosticDriverHost,
    stream: DiagnosticStreamValue
) -> bool {
    return if stream is {
        @Empty {
            false
        }
        @Entry {
            host~>writeDiagnostic(stream~>currentDiagnostic())
        }
    }
}

public procedure isDigitText(text: string@View) -> bool {
    if (diagnosticTextEquals(text, "0")) {
        return true
    }
    if (diagnosticTextEquals(text, "1")) {
        return true
    }
    if (diagnosticTextEquals(text, "2")) {
        return true
    }
    if (diagnosticTextEquals(text, "3")) {
        return true
    }
    if (diagnosticTextEquals(text, "4")) {
        return true
    }
    if (diagnosticTextEquals(text, "5")) {
        return true
    }
    if (diagnosticTextEquals(text, "6")) {
        return true
    }
    if (diagnosticTextEquals(text, "7")) {
        return true
    }
    if (diagnosticTextEquals(text, "8")) {
        return true
    }
    return diagnosticTextEquals(text, "9")
}

public record DecimalWriter {
    public marker: usize

    public procedure writeUnsignedDecimal(~, value: usize) -> bool {
        if (value < 10) {
            return isDigitText(decimalDigitText(value))
        }

        let prefix: usize = value / 10
        return self~>writeUnsignedDecimal(prefix)
    }
}

public procedure decimalDigitText(value: usize) -> string@View
|: value < 10
{
    if (value == 0) {
        return "0"
    }
    if (value == 1) {
        return "1"
    }
    if (value == 2) {
        return "2"
    }
    if (value == 3) {
        return "3"
    }
    if (value == 4) {
        return "4"
    }
    if (value == 5) {
        return "5"
    }
    if (value == 6) {
        return "6"
    }
    if (value == 7) {
        return "7"
    }
    if (value == 8) {
        return "8"
    }
    return "9"
}

public procedure classify(stream: DiagnosticStreamValue) -> i32 {
    if stream is {
        @Empty {
            return 1
        }
        @Entry {
            let current: Diagnostic = stream~>currentDiagnostic()
            if (!current~>isError()) {
                return 2
            }
            if (!diagnosticCodeMatches(current, "E-CLI-0001")) {
                return 3
            }
            if (!diagnosticTextEquals(diagnosticMessage(current), "unknown command")) {
                return 4
            }
            if (!diagnosticSourceIsUnknown(current.source)) {
                return 5
            }
            return if stream~>remainingDiagnostics() is {
                @Empty {
                    0
                }
                @Entry {
                    6
                }
            }
        }
    }
    return 7
}

public procedure classifyCommandResult(result: CommandResultValue) -> i32 {
    return if result is {
        @Succeeded {
            8
        }
        @Failed {
            if (result~>exitCode() != 1) {
                return 9
            }
            classify(result.diagnostics)
        }
    }
}

public procedure main(move ctx: Context) -> i32 {
    let first_write: Outcome<(), IoError> = ctx.io~>write_stderr("diagnostic-")
    let first_write_ok: bool = if first_write is {
        @Value {
            true
        }
        @Error {
            false
        }
    }
    if (!first_write_ok) {
        return 11
    }
    let second_write: Outcome<(), IoError> = ctx.io~>write_stderr("render\n")
    let second_write_ok: bool = if second_write is {
        @Value {
            true
        }
        @Error {
            false
        }
    }
    if (!second_write_ok) {
        return 12
    }

    let host: DiagnosticDriverHost = DiagnosticDriverHost {
        _fs: ctx.io,
        _heap: ctx.heap,
        _sys: ctx.sys,
        _executable_path: ctx.sys~>executable_path(),
        _current_directory: ctx.sys~>current_directory()
    }
    region as diagnostics_region {
        let unknown: CommandResultValue =
            commandFailed(
                diagnostics_region,
                unknownCommandFailure("definitely_unknown_command")
            )
        let unknown_status: i32 = classifyCommandResult(unknown)
        if (unknown_status != 0) {
            return unknown_status
        }
        let diagnostic_written: bool = if unknown is {
            @Succeeded {
                false
            }
            @Failed {
                writeFirstDiagnostic(host, unknown.diagnostics)
            }
        }
        if (!diagnostic_written) {
            return 13
        }
        let writer: DecimalWriter = DecimalWriter {
            marker: 0usize
        }
        if (!writer~>writeUnsignedDecimal(42)) {
            return 10
        }

        let rejected: CommandResultValue =
            commandFailed(
                diagnostics_region,
                rejectedTargetFailure("E-CLI-0001", "unknown command")
            )
        return classifyCommandResult(rejected)
    }
    return 3
}
)uv";
}

}  // namespace

int main() {
  const std::filesystem::path work_root = UV_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "codegen_diagnostic_path_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path tool_root = project_root / "tools";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path run_log = project_root / "run.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" / "diagnostic_path.conformance.log";

  std::error_code ec;
  std::filesystem::remove_all(project_root, ec);
  if (ec) {
    std::cerr << "failed to remove old fixture: " << ec.message() << "\n";
    return 1;
  }
  std::filesystem::create_directories(source_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return 1;
  }

  if (!WriteFile(project_root / "Ultraviolet.toml", FixtureManifest()) ||
      !WriteFile(source_root / "Main.uv", FixtureSource())) {
    return 1;
  }
  if (!InstallTool(tool_root, UV_TEST_LLD_LINK_PATH, "lld-link") ||
      !InstallTool(tool_root, UV_TEST_LLVM_LIB_PATH, "llvm-lib") ||
      !InstallTool(tool_root, UV_TEST_LLVM_AS_PATH, "llvm-as")) {
    return 1;
  }

  const std::string compile_command =
      CommandWithToolPath(
          tool_root,
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " " +
              Quote(project_root.generic_string()) +
              " --assembly diagnostic_path --out-dir " +
              Quote(out_root.generic_string()) +
              " --conformance diagnostic_path.conformance.log" +
              " --build-progress on --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture compile failed; see " << compile_log << "\n";
    return 1;
  }

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return 1;
  }
  if (CountOccurrences(*conformance_text, "\tcodegen\tPatternNarrow-Union\t") == 0) {
    std::cerr << "codegen conformance trace did not record PatternNarrow-Union "
                 "for modal union if-case lowering\n";
    return 1;
  }

  const std::filesystem::path executable =
      out_root / "bin" /
      (std::string("diagnostic_path") +
       std::string(UV_TEST_EXECUTABLE_SUFFIX));
  if (!std::filesystem::exists(executable)) {
    std::cerr << "fixture executable was not produced: " << executable << "\n";
    return 1;
  }

  const std::string run_command = RunExecutableCommand(executable, run_log);
  const int run_result = RunCommand(run_command);
  if (run_result != 0) {
    std::cerr << "fixture executable failed; see " << run_log << "\n";
    return 1;
  }
  const auto run_text = ReadFile(run_log);
  if (!run_text.has_value()) {
    return 1;
  }
  if (run_text->find("diagnostic-render\n") == std::string::npos) {
    std::cerr << "fixture stderr writes were not emitted in order; see "
              << run_log << "\n";
    return 1;
  }
  if (run_text->find("E-CLI-0001 (error): unknown command\n") ==
      std::string::npos) {
    std::cerr << "fixture diagnostic was not rendered completely; see "
              << run_log << "\n";
    return 1;
  }

  const std::filesystem::path ir_file =
      out_root / "ir" / "diagnostic_x5fpath.ll";
  const auto ir_text = ReadFile(ir_file);
  if (!ir_text.has_value()) {
    return 1;
  }

  constexpr std::string_view entry_stub_signature = "define void @main()";
  const auto entry_stub_body = FunctionBody(*ir_text, entry_stub_signature);
  if (!entry_stub_body.has_value()) {
    std::cerr << "executable entry stub was not emitted\n";
    return 1;
  }
  constexpr std::string_view context_init_call =
      "@ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit";
  constexpr std::string_view module_init_call =
      "@ultraviolet_x3a_x3aruntime_x3a_x3ainit_x3a_x3adiagnostic_x5fpath";
  constexpr std::string_view uv_main_call =
      "@diagnostic_x5fpath_x3a_x3amain";
  constexpr std::string_view module_deinit_call =
      "@ultraviolet_x3a_x3aruntime_x3a_x3adeinit_x3a_x3adiagnostic_x5fpath";
  if (!ContainsBefore(*entry_stub_body, context_init_call, module_init_call) ||
      !ContainsBefore(*entry_stub_body, module_init_call, uv_main_call) ||
      !ContainsAfterLast(*entry_stub_body, module_deinit_call, uv_main_call)) {
    std::cerr << "entry stub does not lower ContextInitSigma -> Init(G_e) -> "
                 "main -> Deinit in specification order\n";
    return 1;
  }
  if (CountOccurrences(*entry_stub_body, module_init_call) != 1) {
    std::cerr << "entry stub did not emit exactly one module init call\n";
    return 1;
  }
  if (entry_stub_body->find("entry.deinit.panic.capture") ==
          std::string_view::npos ||
      entry_stub_body->find("entry.deinit.panic.restore") ==
          std::string_view::npos) {
    std::cerr << "entry stub deinit cleanup does not preserve panic-after-cleanup "
                 "semantics\n";
    return 1;
  }

  constexpr std::string_view single_error_signature =
      "@diagnostic_x5fpath_x3a_x3asingleErrorDiagnosticStream";
  const auto single_error_body =
      FunctionBody(*ir_text, single_error_signature);
  if (!single_error_body.has_value()) {
    std::cerr << "singleErrorDiagnosticStream was not emitted\n";
    return 1;
  }
  constexpr std::string_view diagnostic_poison =
      "poison_x3a_x3adiagnostic_x5fpath";
  constexpr std::string_view scope_enter =
      "@ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter";
  constexpr std::string_view region_new_scoped =
      "@ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped";
  if (!ContainsBefore(*single_error_body, diagnostic_poison, scope_enter)) {
    std::cerr << "singleErrorDiagnosticStream does not check module poison "
                 "before entering the body\n";
    return 1;
  }
  if (!ContainsBefore(*single_error_body, "store i8 0", scope_enter) ||
      !ContainsBefore(*single_error_body, "store i32 0", scope_enter)) {
    std::cerr << "singleErrorDiagnosticStream does not clear the panic record "
                 "before entering the body\n";
    return 1;
  }
  if (single_error_body->find(region_new_scoped) != std::string_view::npos) {
    std::cerr << "singleErrorDiagnosticStream emitted a synthetic procedure "
                 "region instead of relying on source region constructs\n";
    return 1;
  }
  constexpr std::string_view scope_exit =
      "@ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit";
  if (ContainsAfterLast(*single_error_body, "panic.take", scope_exit)) {
    std::cerr << "singleErrorDiagnosticStream emits a panic propagation check "
                 "after non-panicking runtime scope cleanup\n";
    return 1;
  }

  constexpr std::string_view host_diagnostic_signature =
      "@diagnostic_x5fpath_x3a_x3aDiagnosticDriverHost_x3a_x3awriteDiagnostic(";
  const auto host_diagnostic_body =
      FunctionBody(*ir_text, host_diagnostic_signature);
  if (!host_diagnostic_body.has_value()) {
    std::cerr << "DiagnosticDriverHost::writeDiagnostic was not emitted\n";
    return 1;
  }
  if (HasByteBitwiseNot(*host_diagnostic_body) ||
      !HasAtLeastBoolNots(*host_diagnostic_body, 4)) {
    std::cerr << "DiagnosticDriverHost::writeDiagnostic does not lower "
                 "!bool as logical not for write guards\n";
    return 1;
  }

  constexpr std::string_view host_head_signature =
      "@diagnostic_x5fpath_x3a_x3aDiagnosticDriverHost_x3a_x3awriteDiagnosticHead";
  const auto host_head_body = FunctionBody(*ir_text, host_head_signature);
  if (!host_head_body.has_value()) {
    std::cerr << "DiagnosticDriverHost::writeDiagnosticHead was not emitted\n";
    return 1;
  }
  if (CountOccurrences(*host_head_body, "writeStderrPart") < 4) {
    std::cerr << "DiagnosticDriverHost::writeDiagnosticHead did not preserve "
                 "the full Present arm write sequence\n";
    return 1;
  }
  if (HasByteBitwiseNot(*host_head_body) ||
      !HasAtLeastBoolNots(*host_head_body, 4)) {
    std::cerr << "DiagnosticDriverHost::writeDiagnosticHead does not lower "
                 "!bool as logical not for write guards\n";
    return 1;
  }

  constexpr std::string_view host_span_signature =
      "@diagnostic_x5fpath_x3a_x3aDiagnosticDriverHost_x3a_x3awriteDiagnosticSpan";
  const auto host_span_body = FunctionBody(*ir_text, host_span_signature);
  if (!host_span_body.has_value()) {
    std::cerr << "DiagnosticDriverHost::writeDiagnosticSpan was not emitted\n";
    return 1;
  }
  if (HasByteBitwiseNot(*host_span_body) ||
      !HasAtLeastBoolNots(*host_span_body, 2)) {
    std::cerr << "DiagnosticDriverHost::writeDiagnosticSpan does not lower "
                 "!bool as logical not for write guards\n";
    return 1;
  }

  constexpr std::string_view free_head_signature =
      "@diagnostic_x5fpath_x3a_x3awriteDiagnosticHead";
  const auto free_head_body = FunctionBody(*ir_text, free_head_signature);
  if (!free_head_body.has_value()) {
    std::cerr << "free writeDiagnosticHead was not emitted\n";
    return 1;
  }
  if (CountOccurrences(*free_head_body, "writeStderrPart") < 4) {
    std::cerr << "free writeDiagnosticHead did not preserve the full Present "
                 "arm write sequence\n";
    return 1;
  }
  if (HasByteBitwiseNot(*free_head_body) ||
      !HasAtLeastBoolNots(*free_head_body, 4)) {
    std::cerr << "free writeDiagnosticHead does not lower !bool as logical "
                 "not for write guards\n";
    return 1;
  }

  constexpr std::string_view text_byte_signature =
      "@diagnostic_x5fpath_x3a_x3adiagnosticTextByte";
  const auto text_byte_body = FunctionBody(*ir_text, text_byte_signature);
  if (!text_byte_body.has_value()) {
    std::cerr << "diagnosticTextByte was not emitted\n";
    return 1;
  }
  if (!CheckFailBlocksDeferToPanicCheck(*text_byte_body) ||
      text_byte_body->find("panic.take") == std::string_view::npos) {
    std::cerr << "diagnosticTextByte checked-index failure path does not "
                 "defer control to the following PanicCheck\n";
    return 1;
  }

  constexpr std::string_view main_signature =
      "@diagnostic_x5fpath_x3a_x3amain";
  const auto main_body = FunctionBody(*ir_text, main_signature);
  if (!main_body.has_value()) {
    std::cerr << "diagnostic_path main was not emitted\n";
    return 1;
  }
  if (main_body->find(region_new_scoped) == std::string_view::npos) {
    std::cerr << "explicit region statement did not emit Region::new_scoped\n";
    return 1;
  }
  if (main_body->find(module_init_call) != std::string_view::npos) {
    std::cerr << "source main body emitted project init; executable lifecycle "
                 "init belongs to the entry stub\n";
    return 1;
  }
  if (HasByteBitwiseNot(*main_body) || !HasAtLeastBoolNots(*main_body, 4)) {
    std::cerr << "diagnostic_path main does not lower !bool as logical not "
                 "for direct bool guards\n";
    return 1;
  }

  constexpr std::string_view code_global =
      "@diagnostic_x5fpath_x3a_x3aCODE_x5fCLI_x5fUNKNOWN_x5fCOMMAND";
  const std::size_t code_global_pos = ir_text->find(code_global);
  if (code_global_pos == std::string::npos) {
    std::cerr << "diagnostic code static was not emitted\n";
    return 1;
  }
  const std::size_t code_global_line_end = ir_text->find('\n', code_global_pos);
  const std::string_view code_global_line =
      std::string_view(*ir_text).substr(
          code_global_pos,
          code_global_line_end == std::string::npos
              ? std::string_view(*ir_text).size() - code_global_pos
              : code_global_line_end - code_global_pos);
  if (code_global_line.find("{ ptr, i64 } zeroinitializer") ==
          std::string_view::npos ||
      code_global_line.find("align 8") == std::string_view::npos) {
    std::cerr << "diagnostic code static is not emitted as an aligned "
                 "string@View global\n";
    return 1;
  }

  constexpr std::string_view init_signature =
      "define void @ultraviolet_x3a_x3aruntime_x3a_x3ainit_x3a_x3a"
      "diagnostic_x5fpath(ptr %__panic)";
  const auto init_body = FunctionBody(*ir_text, init_signature);
  if (!init_body.has_value()) {
    std::cerr << "diagnostic_path module init was not emitted\n";
    return 1;
  }
  if (init_body->find("poison.take") != std::string_view::npos) {
    std::cerr << "string@View static cleanup emitted a module poison read "
                 "inside init panic handling\n";
    return 1;
  }
  if (init_body->find("init.panic.take") == std::string_view::npos) {
    std::cerr << "module static initializer did not emit InitPanicHandle\n";
    return 1;
  }
  if (init_body->find("\npanic.take") != std::string_view::npos) {
    std::cerr << "module init emitted ordinary PanicCheck handling where "
                 "InitPanicHandle is required\n";
    return 1;
  }

  constexpr std::string_view drop_entry_signature =
      "define linkonce_odr dso_local void "
      "@ultraviolet_x3a_x3aruntime_x3a_x3adrop_x3a_x3a"
      "diagnostic_x5fpath_x3a_x3aDiagnosticStream_x3a_x3aEntry"
      "(ptr %data, ptr %__panic)";
  const auto drop_entry_body = FunctionBody(*ir_text, drop_entry_signature);
  if (drop_entry_body.has_value() &&
      drop_entry_body->find("load ptr, ptr %data1") == std::string_view::npos) {
    std::cerr << "DiagnosticStream@Entry drop glue does not read %data\n";
    return 1;
  }
  if (drop_entry_body.has_value() &&
      drop_entry_body->find("ptr null") != std::string_view::npos) {
    std::cerr << "DiagnosticStream@Entry drop glue still passes null data\n";
    return 1;
  }

  return 0;
}
