// =============================================================================
// MIGRATION MAPPING: symbols.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 2.5 "Output Artifacts and Linking" - Object File Naming (lines 1349-1357)
//     - PathToPrefix(s) = Concat([BMap(b) | b in Utf8(NFC(s))])
//     - BMap(b): b in [0-9A-Za-z] -> chr(b), otherwise -> "_x" ++ Hex2(b)
//     - mangle(s) = PathToPrefix(s)
//     - MangleModulePath(p) = mangle(PathString(PathKey(p)))
//   - Section 6.3 "Symbols, Mangling, and Linkage" (referenced in hash.cpp)
//     - Symbol generation and naming conventions
//   - Section 3.1.6 "NFC Normalization" (lines 1838-1851)
//     - NFC(s) = UnicodeNFC_{15.0.0}(s)
//     - IdKey(s) = NFC(s)
//     - PathKey(p) = [NFC(c_1), ..., NFC(c_n)]
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/symbols.cpp
//   - Lines 1-105 (entire file)
//
// CONTENT TO MIGRATE:
//   - IsAsciiAlnum(c) -> bool (lines 11-14) [internal]
//     Checks if character is ASCII alphanumeric [0-9A-Za-z]
//   - HexDigit(value) -> char (lines 16-22) [internal]
//     Converts 0-15 to hex digit '0'-'9' or 'a'-'f' (lowercase)
//   - JoinWithDoubleColon(comps) -> string (lines 24-34) [internal]
//     Joins path components with "::" separator
//   - SplitModulePath(path) -> vector<string> (lines 36-49) [internal]
//     Splits module path on "::" delimiters
//   - StringOfPath(comps) -> string (lines 53-55)
//     Joins vector<string> with "::" via JoinWithDoubleColon
//   - StringOfPath(comps) -> string (lines 57-71)
//     Overload for initializer_list<string_view>
//   - PathToPrefix(s) -> string (lines 73-87)
//     Encodes string for symbol name: alphanumeric pass-through, else "_xHH"
//   - Mangle(s) -> string (lines 89-91)
//     Alias for PathToPrefix
//   - PathSig(comps) -> string (lines 93-95)
//     Mangles joined path: Mangle(StringOfPath(comps))
//   - MangleModulePath(module_path) -> string (lines 97-103)
//     Splits on "::", NFC-normalizes each part, mangles result
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/symbols.h (header)
//     - Function declarations
//   - ultraviolet/include/00_core/unicode.h
//     - NFC() for Unicode normalization
//   - <string>, <string_view>, <vector>, <initializer_list>
//
// REFACTORING NOTES:
//   1. PathToPrefix uses lowercase hex (_xhh) per BMap spec
//   2. Mangle is direct alias for PathToPrefix
//   3. NFC normalization applied per-component in MangleModulePath
//   4. "::" is the module path separator in Ultraviolet
//   5. StringOfPath overloads handle both vector and initializer_list
//   6. No SPEC_DEF/SPEC_RULE macros in source - consider adding:
//      - "PathToPrefix" -> "2.5"
//      - "mangle" -> "2.5"
//      - "MangleModulePath" -> "2.5"
//   7. Consider constexpr where possible for compile-time evaluation
//
// =============================================================================

#include "00_core/symbols.h"

#include <cstddef>

#include "00_core/assert_spec.h"
#include "00_core/unicode.h"

namespace ultraviolet::core {

namespace {

void SpecDefsPathStrings() {
  SPEC_DEF("StringOfPath", "3.4.1");
  SPEC_DEF("StringOfPathRef", "3.4.1");
  SPEC_DEF("PathString", "11.5.3");
}

bool IsAsciiAlnum(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

char HexDigit(unsigned int value) {
  const unsigned int digit = value & 0xF;
  if (digit < 10) {
    return static_cast<char>('0' + digit);
  }
  return static_cast<char>('a' + (digit - 10));
}

std::string JoinWithDoubleColon(const std::vector<std::string>& comps) {
  if (comps.empty()) {
    return "";
  }
  std::string out = comps[0];
  for (std::size_t i = 1; i < comps.size(); ++i) {
    out.append("::");
    out.append(comps[i]);
  }
  return out;
}

std::vector<std::string> SplitModulePath(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t pos = path.find("::", start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(path.substr(start));
      break;
    }
    parts.emplace_back(path.substr(start, pos - start));
    start = pos + 2;
  }
  return parts;
}

}  // namespace

std::string PathString(const std::vector<std::string>& comps) {
  SpecDefsPathStrings();
  return StringOfPath(comps);
}

std::string PathString(std::initializer_list<std::string_view> comps) {
  SpecDefsPathStrings();
  return StringOfPath(comps);
}

std::string StringOfPath(const std::vector<std::string>& comps) {
  SpecDefsPathStrings();
  return JoinWithDoubleColon(comps);
}

std::string StringOfPath(std::initializer_list<std::string_view> comps) {
  SpecDefsPathStrings();
  if (comps.size() == 0) {
    return "";
  }
  std::string out;
  bool first = true;
  for (const auto& comp : comps) {
    if (!first) {
      out.append("::");
    }
    out.append(comp.begin(), comp.end());
    first = false;
  }
  return out;
}

std::string PathToPrefix(std::string_view s) {
  const std::string nfc = NFC(s);
  std::string out;
  out.reserve(nfc.size());
  for (unsigned char c : nfc) {
    if (IsAsciiAlnum(c)) {
      out.push_back(static_cast<char>(c));
    } else {
      out.append("_x");
      out.push_back(HexDigit(c >> 4));
      out.push_back(HexDigit(c));
    }
  }
  return out;
}

std::string Mangle(std::string_view s) {
  return PathToPrefix(s);
}

std::string PathSig(std::initializer_list<std::string_view> comps) {
  return Mangle(PathString(comps));
}

std::string MangleModulePath(std::string_view module_path) {
  auto parts = SplitModulePath(module_path);
  for (auto& part : parts) {
    part = NFC(part);
  }
  return Mangle(PathString(parts));
}

}  // namespace ultraviolet::core
