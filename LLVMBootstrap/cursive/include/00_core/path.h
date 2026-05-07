#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursive::core {

bool IsWinSep(char c);
bool IsAsciiLetter(char c);

bool DriveRooted(std::string_view p);
bool UNC(std::string_view p);
bool RootRelative(std::string_view p);
bool AbsPath(std::string_view p);
bool IsRelative(std::string_view p);

std::string RootTag(std::string_view p);
std::string Tail(std::string_view p);
std::vector<std::string> Segs(std::string_view p);
std::vector<std::string> PathComps(std::string_view p);
std::vector<std::string> DropComps(std::size_t n,
                                   const std::vector<std::string>& comps);
std::optional<std::string> LastComp(const std::vector<std::string>& comps);

std::string JoinComp(const std::vector<std::string>& comps);
std::string Join(std::string_view a, std::string_view b);
std::string Normalize(std::string_view p);
std::optional<std::string> Canon(std::string_view p);

bool PathPrefix(const std::vector<std::string>& path,
                const std::vector<std::string>& pref);
bool Prefix(std::string_view p, std::string_view q);

struct ResolveResult {
  std::string root;
  std::string path;
};

std::optional<ResolveResult> Resolve(std::string_view root, std::string_view p);

std::optional<std::string> Relative(std::string_view p, std::string_view base);

std::string Basename(std::string_view p);
std::string FileExt(std::string_view p);

}  // namespace cursive::core
