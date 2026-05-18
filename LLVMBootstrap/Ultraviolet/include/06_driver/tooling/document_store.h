#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ultraviolet::driver::tooling {

struct DocumentOverlay {
  std::string uri;
  std::filesystem::path path;
  std::int64_t version = 0;
  std::string text_utf8;
};

class DocumentStore {
 public:
  bool Open(std::string uri,
            std::filesystem::path path,
            std::int64_t version,
            std::string text_utf8);
  bool ChangeFull(std::string uri, std::int64_t version, std::string text_utf8);
  bool Close(const std::string& uri);

  std::vector<DocumentOverlay> Overlays() const;
  std::optional<DocumentOverlay> FindByUri(const std::string& uri) const;
  std::optional<DocumentOverlay> FindByPath(
      const std::filesystem::path& path) const;

 private:
  std::unordered_map<std::string, DocumentOverlay> by_uri_;
};

}  // namespace ultraviolet::driver::tooling
