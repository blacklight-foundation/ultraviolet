#include "06_driver/lsp/json_rpc.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace ultraviolet::driver::lsp {

namespace {

void EnsureBinaryStdio() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    done = true;
  }
#endif
}

}  // namespace

std::string StdioJsonRpc::ReadHeaderLine() {
  std::string line;
  std::getline(std::cin, line);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

std::optional<llvm::json::Value> StdioJsonRpc::ReadMessage() {
  EnsureBinaryStdio();

  std::size_t content_length = 0;
  while (std::cin.good()) {
    std::string line = ReadHeaderLine();
    if (line.empty()) {
      break;
    }
    constexpr std::string_view kHeader = "Content-Length:";
    if (line.substr(0, kHeader.size()) == kHeader) {
      std::string value = line.substr(kHeader.size());
      while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
      }
      content_length = static_cast<std::size_t>(std::strtoull(
          value.c_str(), nullptr, 10));
    }
  }

  if (!std::cin.good() || content_length == 0) {
    return std::nullopt;
  }

  std::string body(content_length, '\0');
  std::cin.read(body.data(), static_cast<std::streamsize>(body.size()));
  if (static_cast<std::size_t>(std::cin.gcount()) != body.size()) {
    return std::nullopt;
  }

  auto parsed = llvm::json::parse(body);
  if (!parsed) {
    llvm::errs() << "uv-lsp: JSON parse error: "
                 << llvm::toString(parsed.takeError()) << "\n";
    return std::nullopt;
  }
  return std::move(*parsed);
}

void StdioJsonRpc::WriteMessage(const llvm::json::Value& message) {
  EnsureBinaryStdio();
  std::string body;
  llvm::raw_string_ostream os(body);
  os << message;
  os.flush();

  std::cout << "Content-Length: " << body.size() << "\r\n\r\n";
  std::cout << body;
  std::cout.flush();
}

}  // namespace ultraviolet::driver::lsp
