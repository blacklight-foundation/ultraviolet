// =============================================================================
// version.cpp - Compiler version information
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §0.1 - Document header and version information
//   CursiveSpecification.md §0.3 - Observable Compiler Behavior
//   CursiveSpecification.md §0.4 - Document Scope
//
// =============================================================================

#include "06_driver/version.h"

#include <iostream>
#include <string>

#include "00_core/assert_spec.h"

namespace cursive::driver {

namespace {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;
constexpr const char* VERSION_PRERELEASE = "alpha";
constexpr const char* COMPILER_NAME = "Cursive";

}  // namespace

void SpecDefsDriver() {
  SPEC_DEF("Status", "0.3.2");
  SPEC_DEF("ExitCode", "0.3.2");
}

std::string GetVersionString() {
  std::string version = COMPILER_NAME;
  version += " ";
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
  return "Cursive0 Language Specification";
}

std::string GetBuildInfo() {
  // Build info may be injected by the build system
  // For now, return empty string
  return "";
}

void PrintVersion() {
  std::cout << GetVersionString() << "\n";
  std::cout << "Cursive Language Compiler\n";
  std::cout << "Spec: " << GetSpecVersionString() << "\n";
  const std::string build_info = GetBuildInfo();
  if (!build_info.empty()) {
    std::cout << build_info << "\n";
  }
}

}  // namespace cursive::driver
