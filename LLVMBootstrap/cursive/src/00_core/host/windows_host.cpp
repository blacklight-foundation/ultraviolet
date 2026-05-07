#include "host_internal.h"

#ifdef _WIN32

#include <io.h>
#include <process.h>
#include <windows.h>

#include <array>
#include <string>
#include <thread>
#include <vector>

namespace cursive::core {

namespace {

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
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

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
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring QuoteArg(std::wstring_view arg) {
  if (arg.empty()) {
    return L"\"\"";
  }
  bool needs_quotes = false;
  for (const wchar_t ch : arg) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return std::wstring(arg);
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
  return out;
}

std::wstring BuildCommandLine(const std::filesystem::path& program,
                              const std::vector<std::string>& arguments) {
  std::wstring cmd = QuoteArg(program.wstring());
  for (const auto& arg : arguments) {
    cmd.push_back(L' ');
    cmd += QuoteArg(Utf8ToWide(arg));
  }
  return cmd;
}

void DrainPipe(HANDLE pipe, std::string* output) {
  std::array<char, 4096> buffer{};
  DWORD bytes_read = 0;
  while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytes_read, nullptr) &&
         bytes_read > 0) {
    output->append(buffer.data(), buffer.data() + bytes_read);
  }
  CloseHandle(pipe);
}

HostProcessResult RunProcessImpl(const HostProcessSpec& spec) {
  HostProcessResult result;
  if (spec.program.empty()) {
    result.error_message = "process program path is empty";
    return result;
  }

  const std::wstring cmd = BuildCommandLine(spec.program, spec.arguments);
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  const bool capture_merged =
      spec.output_mode == HostProcessOutputMode::CaptureMerged;
  const bool capture_separate =
      spec.output_mode == HostProcessOutputMode::CaptureSeparate;

  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;
  STARTUPINFOW si{};
  si.cb = sizeof(si);

  if (capture_merged || capture_separate) {
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
      result.error_message = "CreatePipe failed";
      return result;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    if (capture_separate) {
      if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
        result.error_message = "CreatePipe failed";
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return result;
      }
      SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
    }
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdout_write;
    si.hStdError = capture_merged ? stdout_write : stderr_write;
  }

  PROCESS_INFORMATION pi{};
  const DWORD creation_flags = spec.hide_window ? CREATE_NO_WINDOW : 0;
  const std::wstring cwd =
      spec.working_directory.has_value()
          ? spec.working_directory->wstring()
          : std::wstring();

  const BOOL ok = CreateProcessW(
      spec.program.wstring().c_str(), cmd_buf.data(), nullptr, nullptr,
      capture_merged || capture_separate, creation_flags,
      nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);

  if (stdout_write != nullptr) {
    CloseHandle(stdout_write);
  }
  if (stderr_write != nullptr) {
    CloseHandle(stderr_write);
  }

  if (!ok) {
    if (stdout_read != nullptr) {
      CloseHandle(stdout_read);
    }
    if (stderr_read != nullptr) {
      CloseHandle(stderr_read);
    }
    result.error_message =
        "CreateProcessW failed err=" +
        std::to_string(static_cast<unsigned long>(GetLastError()));
    return result;
  }

  result.launched = true;

  if (capture_merged) {
    DrainPipe(stdout_read, &result.output);
  } else if (capture_separate) {
    std::thread stdout_thread(DrainPipe, stdout_read, &result.stdout_text);
    std::thread stderr_thread(DrainPipe, stderr_read, &result.stderr_text);
    WaitForSingleObject(pi.hProcess, INFINITE);
    stdout_thread.join();
    stderr_thread.join();
    result.output = result.stdout_text;
    result.output += result.stderr_text;
  }

  if (!capture_separate) {
    WaitForSingleObject(pi.hProcess, INFINITE);
  }
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return result;
}

HostTerminalInfo QueryTerminalImpl(FILE* stream) {
  HostTerminalInfo info;
  const int fd = _fileno(stream);
  if (fd < 0 || !_isatty(fd)) {
    return info;
  }

  info.is_tty = true;
  info.ansi_enabled = true;

  HANDLE handle = INVALID_HANDLE_VALUE;
  if (stream == stderr) {
    handle = GetStdHandle(STD_ERROR_HANDLE);
  } else if (stream == stdout) {
    handle = GetStdHandle(STD_OUTPUT_HANDLE);
  }
  if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) {
      SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
      CONSOLE_SCREEN_BUFFER_INFO screen_info;
      if (GetConsoleScreenBufferInfo(handle, &screen_info)) {
        info.width = static_cast<int>(screen_info.dwSize.X);
      }
    }
  }
  return info;
}

std::filesystem::path CompilerSidecarBinDirImpl(
    CompilerSupportLayoutKind layout,
    const std::filesystem::path& support_root) {
  if (layout == CompilerSupportLayoutKind::None || support_root.empty()) {
    return {};
  }
  if (layout == CompilerSupportLayoutKind::PackagedOut) {
    return support_root / "windows" / "bin";
  }
  return support_root / "bin";
}

bool ConfigureCompilerSidecarBinDirImpl(const std::filesystem::path& dir,
                                        std::string* error_message) {
  if (dir.empty()) {
    if (error_message != nullptr) {
      error_message->clear();
    }
    return true;
  }
  for (const auto& dll : {"icudt72.dll", "icuuc72.dll", "icuin72.dll"}) {
    const auto dll_path = dir / dll;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll_path, ec) || ec) {
      if (error_message != nullptr) {
        *error_message =
            "Missing compiler sidecar file: " + WideToUtf8Lossy(dll_path.wstring());
      }
      return false;
    }
  }

  if (!SetDllDirectoryW(dir.wstring().c_str())) {
    if (error_message != nullptr) {
      *error_message =
          "Failed to configure compiler sidecar directory: " +
          WideToUtf8Lossy(dir.wstring());
    }
    return false;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

}  // namespace

HostServices BuildWindowsHostServices() {
  HostServices services;
  services.getenv_utf8 = [](std::string_view name) -> std::optional<std::string> {
    if (name.empty()) {
      return std::nullopt;
    }
    const std::wstring wide_name = Utf8ToWide(name);
    const DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (size == 0) {
      return std::nullopt;
    }
    std::wstring value(static_cast<std::size_t>(size), L'\0');
    const DWORD written =
        GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
    if (written == 0 || written >= size) {
      return std::nullopt;
    }
    value.resize(static_cast<std::size_t>(written));
    return WideToUtf8Lossy(value);
  };
  services.path_list_separator = []() { return ';'; };
  services.tool_name_candidates = [](std::string_view tool) {
    std::vector<std::string> out;
    out.emplace_back(tool);
    if (tool.size() < 4 ||
        !(tool.ends_with(".exe") || tool.ends_with(".EXE") ||
          tool.ends_with(".Exe") || tool.ends_with(".eXe"))) {
      out.push_back(std::string(tool) + ".exe");
    }
    return out;
  };
  services.current_executable_path = []() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, path.data(),
                                         static_cast<DWORD>(path.size()));
    if (len == 0 || len >= path.size()) {
      return std::filesystem::path();
    }
    path.resize(static_cast<std::size_t>(len));
    return std::filesystem::path(path);
  };
  services.current_process_id = []() {
    return static_cast<unsigned long>(_getpid());
  };
  services.current_thread_id = []() {
    return static_cast<std::uint64_t>(::GetCurrentThreadId());
  };
  services.run_process = RunProcessImpl;
  services.terminal_info = QueryTerminalImpl;
  services.compiler_sidecar_bin_dir = CompilerSidecarBinDirImpl;
  services.configure_compiler_sidecar_bin_dir = ConfigureCompilerSidecarBinDirImpl;
  return services;
}

const HostServices& NativeHostServices() {
  static const HostServices services = BuildWindowsHostServices();
  return services;
}

}  // namespace cursive::core

#endif
