#include "06_driver/lsp/server.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "01_project/target_profile.h"
#include "06_driver/version.h"

namespace {

void PrintUsage() {
  std::cout << "Ultraviolet language server\n"
            << "\n"
            << "USAGE\n"
            << "  uv-lsp [--stdio] [--log-file <path>] "
               "[--target-profile <profile>]\n"
            << "  uv-lsp --version\n"
            << "\n"
            << "OPTIONS\n"
            << "  --stdio           Run LSP over standard input/output (default)\n"
            << "  --log-file <path> Write server lifecycle logs to <path>\n"
            << "  --target-profile <profile>\n"
            << "                    Select target profile: x86_64-sysv,\n"
            << "                    x86_64-win64, aarch64-aapcs64\n"
            << "  --version         Print version and exit\n"
            << "  -h, --help        Show this help message\n";
}

}  // namespace

int main(int argc, char** argv) {
  ultraviolet::driver::lsp::LspServerOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--stdio") {
      continue;
    }
    if (arg == "--version") {
      std::cout << ultraviolet::driver::GetVersionString()
                << " language server\n";
      return 0;
    }
    if (arg == "-h" || arg == "--help") {
      PrintUsage();
      return 0;
    }
    if (arg == "--log-file") {
      if (i + 1 >= argc) {
        std::cerr << "uv-lsp: --log-file requires a path\n";
        return 2;
      }
      options.log_file = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--target-profile") {
      if (i + 1 >= argc) {
        std::cerr << "uv-lsp: --target-profile requires a profile\n";
        return 2;
      }
      const auto parsed = ultraviolet::project::ParseTargetProfile(argv[++i]);
      if (!parsed.has_value()) {
        std::cerr << "uv-lsp: unknown target profile: " << argv[i] << "\n";
        return 2;
      }
      options.target_profile = *parsed;
      continue;
    }
    std::cerr << "uv-lsp: unknown option: " << arg << "\n";
    return 2;
  }

  ultraviolet::driver::lsp::LspServer server(std::move(options));
  return server.Run();
}
