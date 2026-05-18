#include "06_driver/tooling/uri.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace ultraviolet::driver::tooling {

namespace {

bool IsHex(char ch) {
  return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return 0;
}

std::string PercentDecode(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size() && IsHex(value[i + 1]) &&
        IsHex(value[i + 2])) {
      out.push_back(static_cast<char>((HexValue(value[i + 1]) << 4) |
                                      HexValue(value[i + 2])));
      i += 2;
      continue;
    }
    out.push_back(value[i]);
  }
  return out;
}

bool IsUnreserved(unsigned char ch) {
  return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' ||
         ch == '~' || ch == '/';
}

std::string PercentEncodePath(std::string_view value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (unsigned char ch : value) {
    if (IsUnreserved(ch) || ch == ':') {
      out << static_cast<char>(ch);
      continue;
    }
    out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
  }
  return out.str();
}

}  // namespace

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::path absolute = path;
  if (!absolute.is_absolute()) {
    absolute = std::filesystem::absolute(path, ec);
    if (ec) {
      absolute = path;
    }
  }
  return absolute.lexically_normal();
}

std::string PathKey(const std::filesystem::path& path) {
  std::string key = NormalizePath(path).generic_string();
#ifdef _WIN32
  for (char& ch : key) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
#endif
  return key;
}

std::string PathToFileUri(const std::filesystem::path& path) {
  std::string generic = NormalizePath(path).generic_string();
#ifdef _WIN32
  if (generic.size() >= 2 && std::isalpha(static_cast<unsigned char>(generic[0])) &&
      generic[1] == ':') {
    generic.insert(generic.begin(), '/');
  }
#endif
  return "file://" + PercentEncodePath(generic);
}

std::optional<std::filesystem::path> FileUriToPath(std::string_view uri) {
  constexpr std::string_view kPrefix = "file://";
  if (uri.substr(0, kPrefix.size()) != kPrefix) {
    return std::nullopt;
  }

  std::string path = PercentDecode(uri.substr(kPrefix.size()));
#ifdef _WIN32
  if (path.size() >= 3 && path[0] == '/' &&
      std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
    path.erase(path.begin());
  }
#endif
  return NormalizePath(std::filesystem::path(path));
}

}  // namespace ultraviolet::driver::tooling
