#include "host_internal.h"

#ifndef _WIN32

#include <mach-o/dyld.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <vector>

namespace ultraviolet::core {

namespace {

bool FileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

void ReadPipeToString(int fd, std::string* output) {
  std::array<char, 4096> buffer{};
  ssize_t bytes_read = 0;
  while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
    output->append(buffer.data(),
                   buffer.data() + static_cast<std::size_t>(bytes_read));
  }
  close(fd);
}

HostProcessResult RunProcessImpl(const HostProcessSpec& spec) {
  HostProcessResult result;
  if (spec.program.empty()) {
    result.error_message = "process program path is empty";
    return result;
  }

  const bool capture_merged =
      spec.output_mode == HostProcessOutputMode::CaptureMerged;
  const bool capture_separate =
      spec.output_mode == HostProcessOutputMode::CaptureSeparate;

  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (capture_merged || capture_separate) {
    if (pipe(stdout_pipe) != 0) {
      result.error_message = "pipe failed";
      return result;
    }
    if (capture_separate && pipe(stderr_pipe) != 0) {
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      result.error_message = "pipe failed";
      return result;
    }
  }

  std::vector<std::string> args;
  args.reserve(spec.arguments.size() + 1);
  args.push_back(spec.program.string());
  args.insert(args.end(), spec.arguments.begin(), spec.arguments.end());

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    if (stdout_pipe[0] >= 0) {
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
    }
    if (stderr_pipe[0] >= 0) {
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
    }
    result.error_message = "fork failed";
    return result;
  }

  if (pid == 0) {
    if (spec.working_directory.has_value()) {
      (void)chdir(spec.working_directory->c_str());
    }
    if (capture_merged || capture_separate) {
      close(stdout_pipe[0]);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      if (capture_merged) {
        dup2(stdout_pipe[1], STDERR_FILENO);
      } else {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);
      }
      close(stdout_pipe[1]);
    }
    execv(argv[0], argv.data());
    _exit(127);
  }

  result.launched = true;

  std::thread stdout_thread;
  std::thread stderr_thread;
  if (capture_merged) {
    close(stdout_pipe[1]);
    ReadPipeToString(stdout_pipe[0], &result.output);
  } else if (capture_separate) {
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    stdout_thread = std::thread(ReadPipeToString, stdout_pipe[0],
                                &result.stdout_text);
    stderr_thread = std::thread(ReadPipeToString, stderr_pipe[0],
                                &result.stderr_text);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    if (stdout_thread.joinable()) {
      stdout_thread.join();
    }
    if (stderr_thread.joinable()) {
      stderr_thread.join();
    }
    result.error_message = "waitpid failed";
    return result;
  }
  if (stdout_thread.joinable()) {
    stdout_thread.join();
  }
  if (stderr_thread.joinable()) {
    stderr_thread.join();
  }
  if (capture_separate) {
    result.output = result.stdout_text;
    result.output += result.stderr_text;
  }
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = -1;
  }
  return result;
}

HostTerminalInfo QueryTerminalImpl(FILE* stream) {
  HostTerminalInfo info;
  const int fd = fileno(stream);
  if (fd < 0 || isatty(fd) == 0) {
    return info;
  }
  info.is_tty = true;
  info.ansi_enabled = true;

  struct winsize w {};
  if (ioctl(fd, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    info.width = static_cast<int>(w.ws_col);
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
    return support_root / "macos" / "bin";
  }
  return support_root / "bin";
}

}  // namespace

HostServices BuildMacOSHostServices() {
  HostServices services;
  services.getenv_utf8 = [](std::string_view name) -> std::optional<std::string> {
    if (name.empty()) {
      return std::nullopt;
    }
    const std::string name_copy(name);
    const char* value = std::getenv(name_copy.c_str());
    if (value == nullptr) {
      return std::nullopt;
    }
    return std::string(value);
  };
  services.path_list_separator = []() { return ':'; };
  services.tool_name_candidates = [](std::string_view tool) {
    return std::vector<std::string>{std::string(tool)};
  };
  services.current_executable_path = []() {
    std::vector<char> raw_path(4096);
    uint32_t size = static_cast<uint32_t>(raw_path.size());
    if (_NSGetExecutablePath(raw_path.data(), &size) != 0) {
      if (size == 0) {
        return std::filesystem::path();
      }
      raw_path.assign(static_cast<std::size_t>(size), '\0');
      if (_NSGetExecutablePath(raw_path.data(), &size) != 0) {
        return std::filesystem::path();
      }
    }

    char* resolved_path = realpath(raw_path.data(), nullptr);
    if (resolved_path == nullptr) {
      return std::filesystem::path(raw_path.data());
    }
    std::filesystem::path result(resolved_path);
    std::free(resolved_path);
    return result;
  };
  services.current_process_id = []() {
    return static_cast<unsigned long>(getpid());
  };
  services.current_thread_id = []() {
    std::uint64_t thread_id = 0;
    (void)pthread_threadid_np(nullptr, &thread_id);
    return thread_id;
  };
  services.run_process = RunProcessImpl;
  services.terminal_info = QueryTerminalImpl;
  services.compiler_sidecar_bin_dir = CompilerSidecarBinDirImpl;
  services.configure_compiler_sidecar_bin_dir =
      [](const std::filesystem::path& sidecar_bin_dir,
         std::string* error_message) {
        (void)sidecar_bin_dir;
        if (error_message != nullptr) {
          error_message->clear();
        }
        return true;
      };
  return services;
}

const HostServices& NativeHostServices() {
  static const HostServices services = BuildMacOSHostServices();
  return services;
}

}  // namespace ultraviolet::core

#endif
