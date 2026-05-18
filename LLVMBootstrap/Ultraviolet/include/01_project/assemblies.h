#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/project.h"

namespace ultraviolet::project {

bool IsValidAssemblyKind(std::string_view kind);
bool IsValidLinkKind(std::string_view link_kind);

bool IsExecutable(const Assembly& assembly);
bool IsLibrary(const Assembly& assembly);
bool IsDependency(const Assembly& assembly);
bool IsLinkable(const Assembly& assembly);
bool IsSharedLibrary(const Assembly& assembly);
bool IsStaticLibrary(const Assembly& assembly);

bool IsExecutable(const Project& project);
bool IsLibrary(const Project& project);
bool IsDependency(const Project& project);
bool IsLinkable(const Project& project);
bool IsSharedLibrary(const Project& project);
bool IsStaticLibrary(const Project& project);

std::vector<std::string> GetAssemblyNames(const Project& project);
std::optional<Assembly> GetAssemblyByName(const Project& project,
                                          std::string_view name);

}  // namespace ultraviolet::project
