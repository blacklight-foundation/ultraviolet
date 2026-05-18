#include "06_driver/tooling/document_store.h"

#include "06_driver/tooling/uri.h"

namespace ultraviolet::driver::tooling {

bool DocumentStore::Open(std::string uri,
                         std::filesystem::path path,
                         std::int64_t version,
                         std::string text_utf8) {
  DocumentOverlay overlay;
  overlay.uri = uri;
  overlay.path = NormalizePath(path);
  overlay.version = version;
  overlay.text_utf8 = std::move(text_utf8);
  by_uri_[overlay.uri] = std::move(overlay);
  return true;
}

bool DocumentStore::ChangeFull(std::string uri,
                               std::int64_t version,
                               std::string text_utf8) {
  auto it = by_uri_.find(uri);
  if (it == by_uri_.end()) {
    return false;
  }
  it->second.version = version;
  it->second.text_utf8 = std::move(text_utf8);
  return true;
}

bool DocumentStore::Close(const std::string& uri) {
  return by_uri_.erase(uri) > 0;
}

std::vector<DocumentOverlay> DocumentStore::Overlays() const {
  std::vector<DocumentOverlay> overlays;
  overlays.reserve(by_uri_.size());
  for (const auto& [_, overlay] : by_uri_) {
    overlays.push_back(overlay);
  }
  return overlays;
}

std::optional<DocumentOverlay> DocumentStore::FindByUri(
    const std::string& uri) const {
  const auto it = by_uri_.find(uri);
  if (it == by_uri_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DocumentOverlay> DocumentStore::FindByPath(
    const std::filesystem::path& path) const {
  const std::string key = PathKey(path);
  for (const auto& [_, overlay] : by_uri_) {
    if (PathKey(overlay.path) == key) {
      return overlay;
    }
  }
  return std::nullopt;
}

}  // namespace ultraviolet::driver::tooling
