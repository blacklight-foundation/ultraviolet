#pragma once

#include <string>

namespace ultraviolet::driver {

// Initialize spec definitions for driver module
void SpecDefsDriver();

// Get version string
std::string GetVersionString();

// Get spec version string
std::string GetSpecVersionString();

// Get build info
std::string GetBuildInfo();

// Print version information
void PrintVersion();

}  // namespace ultraviolet::driver
