// =============================================================================
// Calling Conventions (§6.2.1)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.1 Default Calling Convention
//   - CallConvDefault = UltravioletABI (Win64 x86_64-pc-windows-msvc)
//   - FFI calling conventions: "C", "C-unwind", "system", "stdcall",
//     "fastcall", "vectorcall"
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "00_core/spec_trace.h"

#include <unordered_map>

namespace ultraviolet::codegen {

namespace {

// Map from ABI string to CallingConvention enum.
const std::unordered_map<std::string_view, CallingConvention>& AbiStringMap() {
  static const std::unordered_map<std::string_view, CallingConvention> map = {
      {"C", CallingConvention::C},
      {"C-unwind", CallingConvention::CUnwind},
      {"system", CallingConvention::System},
      {"stdcall", CallingConvention::Stdcall},
      {"fastcall", CallingConvention::Fastcall},
      {"vectorcall", CallingConvention::Vectorcall},
  };
  return map;
}

}  // namespace

std::optional<CallingConvention> ParseCallingConvention(std::string_view abi_string) {
  // Default Ultraviolet calling convention.
  if (abi_string.empty()) {
    return CallingConvention::Ultraviolet;
  }

  const auto& map = AbiStringMap();
  const auto it = map.find(abi_string);
  if (it != map.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::string_view CallingConventionName(CallingConvention cc) {
  switch (cc) {
    case CallingConvention::Ultraviolet:
      return "Ultraviolet";
    case CallingConvention::C:
      return "C";
    case CallingConvention::CUnwind:
      return "C-unwind";
    case CallingConvention::System:
      return "system";
    case CallingConvention::Stdcall:
      return "stdcall";
    case CallingConvention::Fastcall:
      return "fastcall";
    case CallingConvention::Vectorcall:
      return "vectorcall";
  }
  return "unknown";
}

bool IsUnwindingConvention(CallingConvention cc) {
  // Only C-unwind allows unwinding across FFI boundary.
  return cc == CallingConvention::CUnwind;
}

bool IsCCompatible(CallingConvention cc) {
  // C-compatible conventions can interop with C code.
  switch (cc) {
    case CallingConvention::C:
    case CallingConvention::CUnwind:
    case CallingConvention::System:
    case CallingConvention::Stdcall:
    case CallingConvention::Fastcall:
    case CallingConvention::Vectorcall:
      return true;
    case CallingConvention::Ultraviolet:
      return false;
  }
  return false;
}

}  // namespace ultraviolet::codegen
