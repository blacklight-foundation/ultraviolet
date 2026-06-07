// =============================================================================
// version.cpp - Compiler version information
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §0.1 - Document header and version information
//   Docs/SPECIFICATION.md §0.3 - Observable Compiler Behavior
//   Docs/SPECIFICATION.md §0.4 - Document Scope
//
// =============================================================================

#include "06_driver/version.h"

#include <iostream>
#include <string>

#include "00_core/assert_spec.h"

namespace ultraviolet::driver {

namespace {

#ifndef UV_COMPILER_VERSION_OVERRIDE
#define UV_COMPILER_VERSION_OVERRIDE ""
#endif

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 4;
constexpr int VERSION_PATCH = 0;
constexpr const char* VERSION_PRERELEASE = "alpha";
constexpr const char* VERSION_OVERRIDE = UV_COMPILER_VERSION_OVERRIDE;
constexpr const char* COMPILER_NAME = "Ultraviolet";

}  // namespace

void SpecDefsDriver() {
  SPEC_DEF("Status", "0.3.2");
  SPEC_DEF("ExitCode", "0.3.2");
}

std::string GetVersionString() {
  std::string version = COMPILER_NAME;
  version += " ";
  if (VERSION_OVERRIDE && VERSION_OVERRIDE[0] != '\0') {
    version += VERSION_OVERRIDE;
    return version;
  }
  version += std::to_string(VERSION_MAJOR);
  version += ".";
  version += std::to_string(VERSION_MINOR);
  version += ".";
  version += std::to_string(VERSION_PATCH);
  if (VERSION_PRERELEASE && VERSION_PRERELEASE[0] != '\0') {
    version += "-";
    version += VERSION_PRERELEASE;
  }
  return version;
}

std::string GetSpecVersionString() {
  return "Ultraviolet Language Specification";
}

std::string GetBuildInfo() {
  // Build info may be injected by the build system
  // For now, return empty string
  return "";
}

void PrintVersion() {
  std::cout << GetVersionString() << "\n";
  std::cout << "Ultraviolet Language Compiler\n";
  std::cout << "Spec: " << GetSpecVersionString() << "\n";
  const std::string build_info = GetBuildInfo();
  if (!build_info.empty()) {
    std::cout << build_info << "\n";
  }
}

}  // namespace ultraviolet::driver
