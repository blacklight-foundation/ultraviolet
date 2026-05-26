#include "crash_debug_internal.h"
#include "00_core/host/services.h"

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <malloc.h>
#include <process.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "dbghelp.lib")

namespace ultraviolet::core::crash_debug_detail {

namespace {

std::string WideToUtf8Lossy(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    std::string out;
    out.reserve(text.size());
    for (const wchar_t ch : text) {
      out.push_back((ch >= 0 && ch <= 0x7F) ? static_cast<char>(ch) : '?');
    }
    return out;
  }
  std::string out(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0);
  if (size <= 0) {
    std::wstring out;
    out.reserve(text.size());
    for (const char ch : text) {
      out.push_back(static_cast<unsigned char>(ch));
    }
    return out;
  }
  std::wstring out(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

std::string ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
      return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:
      return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:
      return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
      return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
      return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
      return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
      return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
      return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:
      return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:
      return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:
      return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:
      return "STACK_OVERFLOW";
    default:
      return "UNKNOWN_EXCEPTION";
  }
}

std::string CrashKindFromException(DWORD code) {
  switch (code) {
    case EXCEPTION_STACK_OVERFLOW:
      return "stack-overflow";
    case EXCEPTION_ACCESS_VIOLATION:
      return "access-violation";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "divide-by-zero";
    default:
      return "seh-exception";
  }
}

std::string DescribeWin32Error(DWORD error_code) {
  if (error_code == 0) {
    return {};
  }
  LPWSTR message_buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD length = FormatMessageW(flags, nullptr, error_code, 0,
                                      reinterpret_cast<LPWSTR>(&message_buffer),
                                      0, nullptr);
  std::string message;
  if (length != 0 && message_buffer != nullptr) {
    message = Trim(WideToUtf8Lossy(std::wstring_view(message_buffer, length)));
  }
  if (message_buffer != nullptr) {
    LocalFree(message_buffer);
  }
  if (message.empty()) {
    message = "Windows error";
  }
  return message + " (" + Hex32(error_code) + ")";
}

std::string QuoteArg(std::wstring_view arg) {
  if (arg.empty()) {
    return "\"\"";
  }
  bool needs_quotes = false;
  for (const wchar_t ch : arg) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return WideToUtf8Lossy(arg);
  }

  std::wstring out;
  out.push_back(L'"');
  int backslashes = 0;
  for (const wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    if (backslashes > 0) {
      out.append(backslashes, L'\\');
      backslashes = 0;
    }
    out.push_back(ch);
  }
  if (backslashes > 0) {
    out.append(backslashes * 2, L'\\');
  }
  out.push_back(L'"');
  return WideToUtf8Lossy(out);
}

std::wstring QuoteWideArg(std::wstring_view arg) {
  return Utf8ToWide(QuoteArg(arg));
}

std::wstring BuildCommandLine(const std::filesystem::path& program,
                              const std::vector<std::string>& arguments) {
  std::wstring cmd = QuoteWideArg(program.wstring());
  for (const auto& arg : arguments) {
    cmd.push_back(L' ');
    const std::wstring wide_arg = Utf8ToWide(arg);
    cmd += QuoteWideArg(wide_arg);
  }
  return cmd;
}

std::string BuildSymbolSearchPath(const std::filesystem::path& executable_path) {
  std::vector<std::string> parts;
  if (!executable_path.empty()) {
    const std::filesystem::path parent = executable_path.parent_path();
    parts.push_back(parent.string());
    if (parent.filename() == "Binary") {
      const std::filesystem::path output_root = parent.parent_path();
      parts.push_back(output_root.string());
      parts.push_back(
          (output_root / "Intermediate" / "Obj").string());
    } else if (parent.filename() == "bin") {
      const std::filesystem::path output_root = parent.parent_path();
      parts.push_back(output_root.string());
      parts.push_back((output_root / "obj").string());
    }
  }
  wchar_t buffer[32767];
  const DWORD len = GetEnvironmentVariableW(
      L"_NT_SYMBOL_PATH", buffer,
      static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
  if (len > 0 && len < (sizeof(buffer) / sizeof(buffer[0]))) {
    parts.push_back(WideToUtf8Lossy(std::wstring_view(buffer, len)));
  }
  std::ostringstream oss;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      oss << ';';
    }
    oss << parts[i];
  }
  return oss.str();
}

struct MapSymbolEntry {
  std::uint64_t rva = 0;
  std::string name;
};

struct MapSymbolIndex {
  std::filesystem::path map_path;
  std::uint64_t preferred_base = 0;
  std::vector<MapSymbolEntry> symbols;
};

constexpr auto kMapFreshnessTolerance = std::chrono::seconds(5);

std::uint64_t FileTimeCacheStamp(const std::filesystem::path& path) {
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return 0;
  }
  const auto ticks = time.time_since_epoch().count();
  if (ticks < 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(ticks);
}

std::string DecodeMapSymbol(std::string_view symbol) {
  std::string out;
  out.reserve(symbol.size());
  for (std::size_t i = 0; i < symbol.size(); ++i) {
    if (i + 4 < symbol.size() && symbol[i] == '_' && symbol[i + 1] == 'x' &&
        std::isxdigit(static_cast<unsigned char>(symbol[i + 2])) != 0 &&
        std::isxdigit(static_cast<unsigned char>(symbol[i + 3])) != 0 &&
        symbol[i + 4] == '_') {
      const auto value = ParseHexU64(symbol.substr(i + 2, 2));
      if (value.has_value() && *value <= 0x7F) {
        out.push_back(static_cast<char>(*value));
        i += 4;
        continue;
      }
    }
    if (i + 2 < symbol.size() && symbol[i] == 'x' &&
        std::isxdigit(static_cast<unsigned char>(symbol[i + 1])) != 0 &&
        std::isxdigit(static_cast<unsigned char>(symbol[i + 2])) != 0 &&
        i > 0 && symbol[i - 1] == '_') {
      const bool has_suffix_boundary =
          (i + 3 == symbol.size()) || symbol[i + 3] == '_' ||
          std::isupper(static_cast<unsigned char>(symbol[i + 3])) != 0;
      if (has_suffix_boundary) {
        const auto value = ParseHexU64(symbol.substr(i + 1, 2));
        if (value.has_value() && *value <= 0x7F) {
          out.push_back(static_cast<char>(*value));
          i += 2;
          if (i + 1 < symbol.size() && symbol[i + 1] == '_') {
            ++i;
          }
          continue;
        }
      }
    }
    out.push_back(symbol[i]);
  }
  return out;
}

std::optional<MapSymbolIndex> LoadMapSymbolIndex(
    const std::filesystem::path& image_path) {
  if (image_path.empty()) {
    return std::nullopt;
  }
  std::error_code ec;
  const auto image_write_time = std::filesystem::last_write_time(image_path, ec);
  if (ec) {
    return std::nullopt;
  }
  std::filesystem::path map_path = image_path;
  map_path.replace_extension(".map");
  if (!std::filesystem::exists(map_path, ec) || ec) {
    return std::nullopt;
  }
  const auto map_write_time = std::filesystem::last_write_time(map_path, ec);
  if (ec) {
    return std::nullopt;
  }
  if (map_write_time + kMapFreshnessTolerance < image_write_time) {
    return std::nullopt;
  }

  std::ifstream in(map_path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  MapSymbolIndex index;
  index.map_path = map_path;

  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = Trim(line);
    static constexpr std::string_view preferred_prefix =
        "Preferred load address is ";
    if (trimmed.rfind(preferred_prefix, 0) == 0) {
      const auto parsed = ParseHexU64(trimmed.substr(preferred_prefix.size()));
      if (parsed.has_value()) {
        index.preferred_base = *parsed;
      }
      continue;
    }

    std::istringstream iss(trimmed);
    std::string address_token;
    std::string symbol_token;
    std::string absolute_token;
    if (!(iss >> address_token >> symbol_token >> absolute_token)) {
      continue;
    }
    if (address_token.find(':') == std::string::npos) {
      continue;
    }
    const auto absolute = ParseHexU64(absolute_token);
    if (!absolute.has_value() || *absolute < index.preferred_base) {
      continue;
    }
    MapSymbolEntry entry;
    entry.rva = *absolute - index.preferred_base;
    entry.name = DecodeMapSymbol(symbol_token);
    index.symbols.push_back(std::move(entry));
  }

  if (index.symbols.empty()) {
    return std::nullopt;
  }
  std::sort(index.symbols.begin(), index.symbols.end(),
            [](const MapSymbolEntry& lhs, const MapSymbolEntry& rhs) {
              return lhs.rva < rhs.rva;
            });
  return index;
}

const MapSymbolIndex* GetCachedMapSymbolIndex(
    const std::filesystem::path& image_path) {
  static std::mutex cache_mutex;
  static std::unordered_map<std::string, std::optional<MapSymbolIndex>> cache;

  const std::string key =
      image_path.lexically_normal().generic_string() + "|" +
      std::to_string(FileTimeCacheStamp(image_path)) + "|" +
      std::to_string(FileTimeCacheStamp(
          std::filesystem::path(image_path).replace_extension(".map")));
  std::lock_guard<std::mutex> lock(cache_mutex);
  auto it = cache.find(key);
  if (it == cache.end()) {
    it = cache.emplace(key, LoadMapSymbolIndex(image_path)).first;
  }
  return it->second.has_value() ? &*it->second : nullptr;
}

void TryResolveFrameFromMap(CrashFrame* frame) {
  if (frame == nullptr || frame->module_path.empty() || frame->module_base == 0 ||
      frame->address < frame->module_base) {
    return;
  }
  const MapSymbolIndex* index =
      GetCachedMapSymbolIndex(std::filesystem::path(frame->module_path));
  if (index == nullptr) {
    return;
  }
  const std::uint64_t rva = frame->address - frame->module_base;
  const auto it = std::upper_bound(
      index->symbols.begin(), index->symbols.end(), rva,
      [](std::uint64_t value, const MapSymbolEntry& entry) {
        return value < entry.rva;
      });
  if (it == index->symbols.begin()) {
    return;
  }
  const MapSymbolEntry& entry = *std::prev(it);
  frame->symbol = entry.name;
  frame->offset = rva - entry.rva;
}

struct SymbolSession {
  HANDLE process = nullptr;
  bool active = false;

  SymbolSession(HANDLE process_handle,
                const std::filesystem::path& executable_path)
      : process(process_handle) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES |
                  SYMOPT_FAIL_CRITICAL_ERRORS);
    const std::string search_path = BuildSymbolSearchPath(executable_path);
    active = SymInitialize(process,
                           search_path.empty() ? nullptr : search_path.c_str(),
                           TRUE) == TRUE;
  }

  ~SymbolSession() {
    if (active) {
      SymCleanup(process);
    }
  }
};

bool WriteMinidump(const CrashArtifacts& artifacts,
                   HANDLE process,
                   DWORD process_id,
                   DWORD thread_id,
                   EXCEPTION_RECORD* exception_record,
                   CONTEXT* context) {
  if (artifacts.minidump_path.empty()) {
    return false;
  }
  EnsureDirectory(artifacts.minidump_path.parent_path());
  HANDLE file = CreateFileW(artifacts.minidump_path.wstring().c_str(),
                            GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_info{};
  MINIDUMP_EXCEPTION_INFORMATION* exception_info_ptr = nullptr;
  EXCEPTION_POINTERS pointers{};
  if (exception_record != nullptr && context != nullptr) {
    pointers.ExceptionRecord = exception_record;
    pointers.ContextRecord = context;
    exception_info.ThreadId = thread_id;
    exception_info.ExceptionPointers = &pointers;
    exception_info.ClientPointers = FALSE;
    exception_info_ptr = &exception_info;
  }

  const BOOL ok = MiniDumpWriteDump(
      process, process_id, file,
      static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                 MiniDumpWithUnloadedModules |
                                 MiniDumpWithDataSegs),
      exception_info_ptr, nullptr, nullptr);
  CloseHandle(file);
  return ok == TRUE;
}

CrashFrame CaptureFrame(HANDLE process,
                        std::uint64_t address,
                        std::size_t index,
                        const std::filesystem::path& executable_path) {
  CrashFrame out;
  out.index = index;
  out.address = address;

  char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  std::memset(symbol_buffer, 0, sizeof(symbol_buffer));
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  if (SymFromAddr(process, address, &displacement, symbol) == TRUE) {
    out.symbol = symbol->Name;
    out.offset = displacement;
  }

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
  DWORD line_displacement = 0;
  if (SymGetLineFromAddr64(process, address, &line_displacement, &line) ==
      TRUE) {
    if (line.FileName != nullptr) {
      out.file = line.FileName;
    }
    out.line = line.LineNumber;
  }

  IMAGEHLP_MODULE64 module{};
  module.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
  if (SymGetModuleInfo64(process, address, &module) == TRUE) {
    out.module_base = module.BaseOfImage;
    if (address >= module.BaseOfImage) {
      out.module_offset = address - module.BaseOfImage;
    }
    if (module.ModuleName[0] != '\0') {
      out.module = module.ModuleName;
    }
    if (module.LoadedImageName[0] != '\0') {
      out.module_path = module.LoadedImageName;
    } else if (module.ImageName[0] != '\0') {
      out.module_path = module.ImageName;
    }
  }

  if (out.module_path.empty() && !executable_path.empty()) {
    out.module_path = executable_path.generic_string();
  }
  if (out.module.empty() && !out.module_path.empty()) {
    out.module = std::filesystem::path(out.module_path).stem().string();
  }
  if (out.symbol.empty()) {
    TryResolveFrameFromMap(&out);
  }
  return out;
}

std::vector<CrashFrame> CaptureFrames(
    HANDLE process,
    HANDLE thread,
    CONTEXT context,
    std::size_t max_frames,
    const std::filesystem::path& executable_path) {
  std::vector<CrashFrame> frames;
  SymbolSession symbols(process, executable_path);
  if (!symbols.active || max_frames == 0) {
    return frames;
  }

  STACKFRAME64 frame{};
  DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;

  frames.push_back(CaptureFrame(process, context.Rip, 0, executable_path));

  std::size_t next_index = 1;
  while (frames.size() < max_frames) {
    const BOOL ok =
        StackWalk64(machine_type, process, thread, &frame, &context, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
    if (!ok || frame.AddrPC.Offset == 0) {
      break;
    }
    if (!frames.empty() && frames.back().address == frame.AddrPC.Offset) {
      continue;
    }
    frames.push_back(CaptureFrame(process, frame.AddrPC.Offset, next_index,
                                  executable_path));
    ++next_index;
  }
  return frames;
}

std::string HumanMessageForException(DWORD code) {
  if (code == EXCEPTION_STACK_OVERFLOW) {
    return "Unhandled stack overflow.";
  }
  return "Unhandled structured exception.";
}

CrashReport CaptureCrashReport(const CrashRuntimeOptions& options,
                               std::string_view kind,
                               HANDLE process,
                               DWORD process_id,
                               HANDLE thread,
                               DWORD thread_id,
                               DWORD exception_code,
                               const std::string& message,
                               EXCEPTION_RECORD* exception_record,
                               CONTEXT* context) {
  const CrashArtifacts artifacts =
      MakeArtifacts(options.report_root.empty() ? TempCrashRoot()
                                                : options.report_root,
                    kind, process_id);
  std::vector<CrashFrame> frames;
  if (context != nullptr && thread != nullptr) {
    frames = CaptureFrames(process, thread, *context, options.max_frames,
                           options.executable_path);
  }
  if (options.write_minidump) {
    WriteMinidump(artifacts, process, process_id, thread_id, exception_record,
                  context);
  }
  return BuildCrashReport(options, kind, process_id, thread_id, exception_code,
                          exception_code == 0 ? std::string{}
                                              : ExceptionName(exception_code),
                          message, artifacts, std::move(frames));
}

void DrainPipe(HANDLE pipe, std::string* output) {
  char buffer[4096];
  DWORD bytes_read = 0;
  while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) &&
         bytes_read > 0) {
    output->append(buffer, buffer + bytes_read);
  }
  CloseHandle(pipe);
}

bool IsBenignFirstChance(DWORD code) {
  return code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP;
}

DebugRunResult DebugRunWindows(const DebugRunOptions& options) {
  DebugRunResult result;
  if (options.program.empty()) {
    return result;
  }
  if (!options.enabled) {
    return RunProcessWithoutDebugger(options);
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;
  if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
    result.launch_error = DescribeWin32Error(GetLastError());
    return result;
  }
  if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
    result.launch_error = DescribeWin32Error(GetLastError());
    CloseHandle(stdout_read);
    CloseHandle(stdout_write);
    return result;
  }
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = stdout_write;
  si.hStdError = stderr_write;

  PROCESS_INFORMATION pi{};
  std::wstring cmd = BuildCommandLine(options.program, options.arguments);
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  const std::wstring cwd =
      options.working_directory.empty()
          ? options.program.parent_path().wstring()
          : options.working_directory.wstring();

  const BOOL ok = CreateProcessW(
      options.program.wstring().c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
      DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr,
      cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);

  CloseHandle(stdout_write);
  CloseHandle(stderr_write);

  if (!ok) {
    result.launch_error = DescribeWin32Error(GetLastError());
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    return result;
  }

  result.launched = true;
  std::thread stdout_thread(DrainPipe, stdout_read, &result.stdout_text);
  std::thread stderr_thread(DrainPipe, stderr_read, &result.stderr_text);

  while (true) {
    DEBUG_EVENT event{};
    if (!WaitForDebugEvent(&event, INFINITE)) {
      break;
    }

    DWORD continue_status = DBG_CONTINUE;
    switch (event.dwDebugEventCode) {
      case CREATE_PROCESS_DEBUG_EVENT:
        if (event.u.CreateProcessInfo.hFile != nullptr) {
          CloseHandle(event.u.CreateProcessInfo.hFile);
        }
        break;
      case LOAD_DLL_DEBUG_EVENT:
        if (event.u.LoadDll.hFile != nullptr) {
          CloseHandle(event.u.LoadDll.hFile);
        }
        break;
      case EXCEPTION_DEBUG_EVENT: {
        const auto& exception = event.u.Exception.ExceptionRecord;
        const bool first_chance = event.u.Exception.dwFirstChance != 0;
        if (first_chance && IsBenignFirstChance(exception.ExceptionCode)) {
          continue_status = DBG_CONTINUE;
          break;
        }
        if (first_chance) {
          continue_status = DBG_EXCEPTION_NOT_HANDLED;
          break;
        }

        HANDLE thread =
            OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                           THREAD_QUERY_INFORMATION,
                       FALSE, event.dwThreadId);
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (thread != nullptr) {
          GetThreadContext(thread, &context);
        }

        const CrashRuntimeOptions runtime_options = {
            options.enabled,
            options.write_minidump,
            options.emit_stderr_summary,
            false,
            options.max_frames,
            options.report_root.empty()
                ? DefaultTargetCrashReportRoot(options.program)
                : options.report_root,
            options.tool_name,
            options.tool_version,
            options.arguments,
            PathString(options.working_directory.empty()
                           ? options.program.parent_path()
                           : options.working_directory),
            options.program};
        const CrashArtifacts artifacts = MakeArtifacts(
            runtime_options.report_root,
            CrashKindFromException(exception.ExceptionCode), pi.dwProcessId);
        auto frames = thread != nullptr
                          ? CaptureFrames(pi.hProcess, thread, context,
                                          options.max_frames, options.program)
                          : std::vector<CrashFrame>{};
        EXCEPTION_RECORD record_copy = exception;
        CONTEXT context_copy = context;
        if (options.write_minidump && thread != nullptr) {
          WriteMinidump(artifacts, pi.hProcess, pi.dwProcessId, event.dwThreadId,
                        &record_copy, &context_copy);
        }
        result.crashed = true;
        result.crash_report = BuildCrashReport(
            runtime_options, CrashKindFromException(exception.ExceptionCode),
            pi.dwProcessId, event.dwThreadId, exception.ExceptionCode,
            ExceptionName(exception.ExceptionCode),
            HumanMessageForException(exception.ExceptionCode), artifacts,
            std::move(frames));
        if (thread != nullptr) {
          CloseHandle(thread);
        }
        continue_status = DBG_EXCEPTION_NOT_HANDLED;
        break;
      }
      case EXIT_PROCESS_DEBUG_EVENT:
        result.exit_code = static_cast<int>(event.u.ExitProcess.dwExitCode);
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
        goto done;
      default:
        break;
    }
    ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continue_status);
  }

done:
  WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  stdout_thread.join();
  stderr_thread.join();

  if (result.crash_report.has_value()) {
    WriteTextFile(result.crash_report->artifacts.stdout_path, result.stdout_text);
    WriteTextFile(result.crash_report->artifacts.stderr_path, result.stderr_text);
    WriteTextFile(result.crash_report->artifacts.text_path,
                  CrashSummary(*result.crash_report));
    WriteTextFile(result.crash_report->artifacts.json_path,
                  CrashReportToJson(*result.crash_report));
    if (options.emit_stderr_summary) {
      const std::string summary = CrashSummary(*result.crash_report);
      std::fwrite(summary.data(), 1, summary.size(), stderr);
      std::fflush(stderr);
    }
  }

  return result;
}

CrashReport CaptureCurrentProcessCrash(const CrashRuntimeOptions& options,
                                       std::string_view kind,
                                       DWORD exception_code,
                                       const std::string& message,
                                       EXCEPTION_RECORD* exception_record,
                                       CONTEXT* context) {
  return CaptureCrashReport(options, kind, GetCurrentProcess(),
                            GetCurrentProcessId(), GetCurrentThread(),
                            GetCurrentThreadId(), exception_code, message,
                            exception_record, context);
}

struct DeferredCrashCaptureRequest {
  HANDLE completion_event = nullptr;
  DWORD thread_id = 0;
  DWORD exception_code = 0;
  bool has_exception_record = false;
  bool has_context = false;
  EXCEPTION_RECORD exception_record{};
  CONTEXT context{};
};

unsigned __stdcall DeferredCrashCaptureThreadProc(void* raw_request) {
  auto* request = static_cast<DeferredCrashCaptureRequest*>(raw_request);
  if (request == nullptr) {
    return 0;
  }

  const CrashRuntimeOptions options = CrashOptionsSnapshot();
  HANDLE crash_thread =
      OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                 request->thread_id);
  CrashReport report = CaptureCrashReport(
      options, CrashKindFromException(request->exception_code),
      GetCurrentProcess(), GetCurrentProcessId(), crash_thread,
      request->thread_id, request->exception_code,
      HumanMessageForException(request->exception_code),
      request->has_exception_record ? &request->exception_record : nullptr,
      request->has_context ? &request->context : nullptr);
  if (crash_thread != nullptr) {
    CloseHandle(crash_thread);
  }
  EmitCrashOutputs(options, report);
  if (request->completion_event != nullptr) {
    SetEvent(request->completion_event);
  }
  return 0;
}

void CaptureCrashOffThread(EXCEPTION_POINTERS* info) {
  auto* request = static_cast<DeferredCrashCaptureRequest*>(
      HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(DeferredCrashCaptureRequest)));
  if (request == nullptr) {
    return;
  }

  request->thread_id = GetCurrentThreadId();
  if (info != nullptr && info->ExceptionRecord != nullptr) {
    request->exception_code = info->ExceptionRecord->ExceptionCode;
    request->exception_record = *info->ExceptionRecord;
    request->has_exception_record = true;
  }
  if (info != nullptr && info->ContextRecord != nullptr) {
    request->context = *info->ContextRecord;
    request->has_context = true;
  }

  request->completion_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (request->completion_event == nullptr) {
    HeapFree(GetProcessHeap(), 0, request);
    return;
  }

  unsigned thread_id = 0;
  HANDLE worker = reinterpret_cast<HANDLE>(
      _beginthreadex(nullptr, 0, DeferredCrashCaptureThreadProc, request, 0,
                     &thread_id));
  if (worker == nullptr) {
    CloseHandle(request->completion_event);
    HeapFree(GetProcessHeap(), 0, request);
    return;
  }

  WaitForSingleObject(request->completion_event, 30000);
  WaitForSingleObject(worker, 30000);
  CloseHandle(worker);
  CloseHandle(request->completion_event);
  HeapFree(GetProcessHeap(), 0, request);
}

LONG WINAPI UnhandledExceptionFilterThunk(EXCEPTION_POINTERS* info) {
  if (!State().options.enabled) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  bool expected = false;
  if (!HandlingCrash().compare_exchange_strong(expected, true)) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  if (info != nullptr && info->ExceptionRecord != nullptr &&
      info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
    _resetstkoflw();
  }
  CaptureCrashOffThread(info);
  return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateHandlerThunk() {
  if (!State().options.enabled) {
    std::abort();
  }
  bool expected = false;
  if (!HandlingCrash().compare_exchange_strong(expected, true)) {
    std::_Exit(3);
  }
  CONTEXT context{};
  RtlCaptureContext(&context);
  CrashReport report = CaptureCurrentProcessCrash(
      State().options, "terminate", 0u,
      "std::terminate was invoked.", nullptr, &context);
  EmitCrashOutputs(State().options, report);
  std::_Exit(3);
}

void AbortSignalHandlerThunk(int) {
  if (!State().options.enabled) {
    std::_Exit(134);
  }
  bool expected = false;
  if (!HandlingCrash().compare_exchange_strong(expected, true)) {
    std::_Exit(134);
  }
  CONTEXT context{};
  RtlCaptureContext(&context);
  CrashReport report = CaptureCurrentProcessCrash(
      State().options, "abort", 0u, "abort() was invoked.", nullptr, &context);
  EmitCrashOutputs(State().options, report);
  std::_Exit(134);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4717)
#endif
[[noreturn]] void TriggerStackOverflowFixture() {
  alignas(16) volatile char padding[4096]{};
  padding[0] = 1;
  (void)padding;
  TriggerStackOverflowFixture();
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace

bool CrashCaptureSupportedBackend() {
  return true;
}

void InstallCrashHandlersBackend() {
  std::lock_guard<std::mutex> lock(StateMutex());
  if (State().installed) {
    return;
  }
  ULONG stack_guarantee = 64 * 1024;
  SetThreadStackGuarantee(&stack_guarantee);
  SetUnhandledExceptionFilter(UnhandledExceptionFilterThunk);
  std::set_terminate(TerminateHandlerThunk);
  std::signal(SIGABRT, AbortSignalHandlerThunk);
  State().installed = true;
}

void MaybeTriggerCrashFixtureFromEnvBackend() {
  const auto value = HostGetEnvUtf8("UV_CRASH_TEST");
  if (!value.has_value() || value->empty()) {
    return;
  }

  if (*value == "stack-overflow") {
    TriggerStackOverflowFixture();
  }
  if (*value == "access-violation") {
    *static_cast<volatile int*>(nullptr) = 1;
  }
  if (*value == "abort") {
    std::abort();
  }
  if (*value == "terminate") {
    std::terminate();
  }
}

DebugRunResult DebugRunProcessBackend(const DebugRunOptions& options) {
  if (!options.enabled) {
    return RunProcessWithoutDebugger(options);
  }
  return DebugRunWindows(options);
}

}  // namespace ultraviolet::core::crash_debug_detail

#endif
