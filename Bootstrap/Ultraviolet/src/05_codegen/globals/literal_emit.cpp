// =============================================================================
// MIGRATION MAPPING: literal_emit.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.3.1 Literal Identity (lines 15427-15434)
//   - LiteralData(kind, contents) constructor (line 15398)
//   - FNV1a64 hash (lines 15429-15432)
//   - LiteralID computation (line 15434)
//   - Mangle-Literal rule (lines 15522-15525)
//   - Linkage-LiteralData rule (lines 15625-15628)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/literal_emit.cpp
//   - String literal data emission
//   - Literal deduplication by content hash
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/globals/literal_emit.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h (GlobalConst)
//   - ultraviolet/include/05_codegen/symbols/mangle.h (MangleLiteral)
//   - ultraviolet/include/00_core/hash.h (FNV1a64, LiteralID)
//
// REFACTORING NOTES:
//   1. Literals are emitted as GlobalConst data
//   2. LiteralID = mangle(kind) + "_" + Hex64(FNV1a64(contents))
//   3. Symbol = PathSig(["ultraviolet", "runtime", "literal", LiteralID])
//   4. Deduplication: identical content -> same symbol
//   5. Linkage is Internal (module-local)
//   6. Literal kinds:
//      - "string" for string literals
//      - "bytes" for byte array literals
//   7. Contents stored as raw bytes
//
// LITERAL EMISSION:
//   1. Compute content hash (FNV1a64)
//   2. Generate LiteralID
//   3. Check if already emitted (dedup)
//   4. Emit GlobalConst if new
//   5. Return symbol for reference
//
// FNV1A64 ALGORITHM:
//   hash = FNVOffset64 (14695981039346656037)
//   for each byte b:
//     hash = (hash XOR b) * FNVPrime64 (1099511628211)
//   return hash
// =============================================================================

#include "05_codegen/globals/literal_emit.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "04_analysis/layout/layout.h"
#include "01_project/language_profile.h"
#include "01_project/target_profile.h"
#include "05_codegen/lower/lower_proc.h"
#include "05_codegen/symbols/mangle.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "00_core/assert_spec.h"
#include "00_core/hash.h"
#include "00_core/spec_trace.h"
#include "00_core/symbols.h"

namespace ultraviolet::codegen {

std::optional<LiteralKind> LiteralKindOfImmediate(const IRValue& value) {
  if (value.kind != IRValue::Kind::Immediate || value.bytes.empty()) {
    return std::nullopt;
  }

  if (value.literal_kind.has_value()) {
    switch (*value.literal_kind) {
      case IRImmediateLiteralKind::String: return LiteralKind::String;
      case IRImmediateLiteralKind::Bytes: return LiteralKind::Bytes;
      case IRImmediateLiteralKind::Char: return LiteralKind::Char;
      case IRImmediateLiteralKind::Int: return LiteralKind::Int;
      case IRImmediateLiteralKind::Float: return LiteralKind::Float;
    }
  }

  const std::string_view lexeme = value.name;
  if (lexeme == "true" || lexeme == "false" || lexeme == "null") {
    return std::nullopt;
  }
  if (lexeme.size() >= 2 && lexeme.front() == '"' && lexeme.back() == '"') {
    return LiteralKind::String;
  }
  if (lexeme.size() >= 2 && lexeme.front() == '\'' && lexeme.back() == '\'') {
    return LiteralKind::Char;
  }

  auto ends_with = [&](std::string_view suffix) -> bool {
    return lexeme.size() >= suffix.size() &&
           lexeme.substr(lexeme.size() - suffix.size()) == suffix;
  };
  const bool looks_float =
      lexeme.find('.') != std::string_view::npos ||
      lexeme.find('e') != std::string_view::npos ||
      lexeme.find('E') != std::string_view::npos ||
      ends_with("f") ||
      ends_with("f16") ||
      ends_with("f32") ||
      ends_with("f64");
  if (looks_float) {
    return LiteralKind::Float;
  }

  return LiteralKind::Int;
}

// ============================================================================
// Section 6.12.14 Literal Kind Classification
// ============================================================================

std::string_view LiteralKindToString(LiteralKind kind) {
  switch (kind) {
    case LiteralKind::String: return "string";
    case LiteralKind::Bytes: return "bytes";
    case LiteralKind::Char: return "char";
    case LiteralKind::Int: return "int";
    case LiteralKind::Float: return "float";
    case LiteralKind::Array: return "array";
  }
  return "unknown";
}

// ============================================================================
// Section 6.12.14 Literal Symbol Generation
// ============================================================================

std::string LiteralSym(LiteralKind kind, const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("LiteralSym");
  return MangleLiteral(std::string(LiteralKindToString(kind)), bytes);
}

std::string LiteralSymString(std::string_view content) {
  std::vector<std::uint8_t> bytes(content.begin(), content.end());
  return LiteralSym(LiteralKind::String, bytes);
}

std::string LiteralSymBytes(const std::vector<std::uint8_t>& content) {
  return LiteralSym(LiteralKind::Bytes, content);
}

bool IsLiteralSymbol(const std::string& symbol) {
  const std::string kLiteralPrefix = project::RuntimePathSig({"literal"});
  return symbol.rfind(kLiteralPrefix, 0) == 0;
}

namespace {

std::string_view LiteralSourceKindName(IRImmediateLiteralKind kind) {
  switch (kind) {
    case IRImmediateLiteralKind::String: return "StringLiteral";
    case IRImmediateLiteralKind::Bytes: return "BytesLiteral";
    case IRImmediateLiteralKind::Char: return "CharLiteral";
    case IRImmediateLiteralKind::Int: return "IntLiteral";
    case IRImmediateLiteralKind::Float: return "FloatLiteral";
  }
  return "UnknownLiteral";
}

bool IsStringLiteralLexeme(std::string_view lexeme) {
  return lexeme.size() >= 2 && lexeme.front() == '"' && lexeme.back() == '"';
}

std::optional<IRImmediateLiteralKind> SourceLiteralKindOfImmediate(
    const IRValue& value,
    LiteralKind emitted_kind) {
  if (emitted_kind == LiteralKind::Bytes && IsStringLiteralLexeme(value.name)) {
    return IRImmediateLiteralKind::String;
  }
  return value.literal_kind;
}

void AppendLiteralPayloadField(std::string& payload,
                               std::string_view key,
                               std::string_view value) {
  if (!payload.empty()) {
    payload += ';';
  }
  payload.append(key.data(), key.size());
  payload += '=';
  payload.append(value.data(), value.size());
}

void AppendLiteralPayloadField(std::string& payload,
                               std::string_view key,
                               std::size_t value) {
  AppendLiteralPayloadField(payload, key, std::to_string(value));
}

void AppendLiteralPayloadField(std::string& payload,
                               std::string_view key,
                               bool value) {
  AppendLiteralPayloadField(
      payload,
      key,
      std::string_view(value ? "true" : "false"));
}

bool IsUtf8ContinuationByte(std::uint8_t byte) {
  return (byte & 0xC0u) == 0x80u;
}

bool IsUtf8Valid(const std::vector<std::uint8_t>& bytes) {
  std::size_t index = 0;
  while (index < bytes.size()) {
    const std::uint8_t first = bytes[index];
    if (first <= 0x7Fu) {
      ++index;
      continue;
    }

    if (first >= 0xC2u && first <= 0xDFu) {
      if (index + 1 >= bytes.size() ||
          !IsUtf8ContinuationByte(bytes[index + 1])) {
        return false;
      }
      index += 2;
      continue;
    }

    if (first == 0xE0u) {
      if (index + 2 >= bytes.size() ||
          bytes[index + 1] < 0xA0u ||
          bytes[index + 1] > 0xBFu ||
          !IsUtf8ContinuationByte(bytes[index + 2])) {
        return false;
      }
      index += 3;
      continue;
    }

    if ((first >= 0xE1u && first <= 0xECu) ||
        (first >= 0xEEu && first <= 0xEFu)) {
      if (index + 2 >= bytes.size() ||
          !IsUtf8ContinuationByte(bytes[index + 1]) ||
          !IsUtf8ContinuationByte(bytes[index + 2])) {
        return false;
      }
      index += 3;
      continue;
    }

    if (first == 0xEDu) {
      if (index + 2 >= bytes.size() ||
          bytes[index + 1] < 0x80u ||
          bytes[index + 1] > 0x9Fu ||
          !IsUtf8ContinuationByte(bytes[index + 2])) {
        return false;
      }
      index += 3;
      continue;
    }

    if (first == 0xF0u) {
      if (index + 3 >= bytes.size() ||
          bytes[index + 1] < 0x90u ||
          bytes[index + 1] > 0xBFu ||
          !IsUtf8ContinuationByte(bytes[index + 2]) ||
          !IsUtf8ContinuationByte(bytes[index + 3])) {
        return false;
      }
      index += 4;
      continue;
    }

    if (first >= 0xF1u && first <= 0xF3u) {
      if (index + 3 >= bytes.size() ||
          !IsUtf8ContinuationByte(bytes[index + 1]) ||
          !IsUtf8ContinuationByte(bytes[index + 2]) ||
          !IsUtf8ContinuationByte(bytes[index + 3])) {
        return false;
      }
      index += 4;
      continue;
    }

    if (first == 0xF4u) {
      if (index + 3 >= bytes.size() ||
          bytes[index + 1] < 0x80u ||
          bytes[index + 1] > 0x8Fu ||
          !IsUtf8ContinuationByte(bytes[index + 2]) ||
          !IsUtf8ContinuationByte(bytes[index + 3])) {
        return false;
      }
      index += 4;
      continue;
    }

    return false;
  }
  return true;
}

std::string LiteralEvidencePayload(
    LiteralKind kind,
    const std::vector<std::uint8_t>& source_bytes,
    const GlobalConst& global,
    std::string_view source,
    std::optional<IRImmediateLiteralKind> source_literal_kind = std::nullopt) {
  const std::string_view kind_name = LiteralKindToString(kind);
  std::string payload;
  AppendLiteralPayloadField(payload, "source", source);
  AppendLiteralPayloadField(payload, "kind", kind_name);
  AppendLiteralPayloadField(payload, "byte_count", source_bytes.size());
  if (source_literal_kind.has_value()) {
    AppendLiteralPayloadField(
        payload,
        "source_literal_kind",
        LiteralSourceKindName(*source_literal_kind));
  }
  if (kind == LiteralKind::Bytes) {
    const bool from_string =
        source_literal_kind.has_value() &&
        *source_literal_kind == IRImmediateLiteralKind::String;
    AppendLiteralPayloadField(
        payload,
        "raw_bytes_from",
        from_string ? std::string_view("StringBytes")
                    : std::string_view("RawBytes"));
  }
  AppendLiteralPayloadField(payload, "global_const", true);
  AppendLiteralPayloadField(payload, "symbol_present", !global.symbol.empty());
  AppendLiteralPayloadField(payload, "bytes_preserved", global.bytes == source_bytes);
  AppendLiteralPayloadField(payload, "string_bytes", kind == LiteralKind::String);
  AppendLiteralPayloadField(payload, "raw_bytes", kind == LiteralKind::Bytes);
  AppendLiteralPayloadField(
      payload,
      "string_raw_bytes_equivalent",
      kind == LiteralKind::String && global.bytes == source_bytes);
  AppendLiteralPayloadField(
      payload,
      "utf8_valid",
      kind == LiteralKind::String && IsUtf8Valid(source_bytes));
  return payload;
}

void RecordLiteralDataEvidence(LiteralKind kind,
                               const std::vector<std::uint8_t>& bytes,
                               const GlobalConst& global,
                               std::string_view source,
                               std::optional<IRImmediateLiteralKind>
                                   source_literal_kind = std::nullopt) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  const std::string payload =
      LiteralEvidencePayload(kind, bytes, global, source, source_literal_kind);
  core::Conformance::Record("def.24.LiteralEmitJudg", std::nullopt, payload);
  core::Conformance::Record(
      "rule.24.EmitLiteralData-Decl", std::nullopt, payload);
  switch (kind) {
    case LiteralKind::Bytes:
      core::Conformance::Record("rule.24.EmitLiteral-Bytes", std::nullopt, payload);
      core::Conformance::Record(
          "req.24.EmitLiteral-Bytes-UndefinedRawBytes", std::nullopt, payload);
      break;
    case LiteralKind::Char:
      core::Conformance::Record("rule.24.EmitLiteral-Char", std::nullopt, payload);
      break;
    case LiteralKind::Int:
      core::Conformance::Record("rule.24.EmitLiteral-Int", std::nullopt, payload);
      break;
    case LiteralKind::Float:
      core::Conformance::Record("rule.24.EmitLiteral-Float", std::nullopt, payload);
      break;
    case LiteralKind::String:
      break;
  }
}

void RecordStringLiteralEvidence(const std::vector<std::uint8_t>& bytes,
                                 const GlobalConst& global,
                                 std::string_view source) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  const std::string payload =
      LiteralEvidencePayload(
          LiteralKind::String,
          bytes,
          global,
          source,
          IRImmediateLiteralKind::String);
  core::Conformance::Record(
      "def.24.StringBytesAndRawBytes", std::nullopt, payload);
  core::Conformance::Record("rule.24.EmitLiteral-String", std::nullopt, payload);
  core::Conformance::Record(
      "req.24.EmitLiteral-String-Utf8Valid", std::nullopt, payload);
}

void RecordBytesLiteralEvidence(const std::vector<std::uint8_t>& bytes,
                                const GlobalConst& global,
                                std::string_view source,
                                std::optional<IRImmediateLiteralKind>
                                    source_literal_kind = std::nullopt) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  const std::string payload =
      LiteralEvidencePayload(
          LiteralKind::Bytes,
          bytes,
          global,
          source,
          source_literal_kind);
  core::Conformance::Record(
      "def.24.StringBytesAndRawBytes", std::nullopt, payload);
  core::Conformance::Record("rule.24.EmitLiteral-Bytes", std::nullopt, payload);
  core::Conformance::Record(
      "req.24.EmitLiteral-Bytes-UndefinedRawBytes", std::nullopt, payload);
}

}  // namespace

// ============================================================================
// Section 6.12.14 Literal IR Declaration Generation
// ============================================================================

IRDecl EmitLiteralData(LiteralKind kind, const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("EmitLiteralData-Decl");

  GlobalConst gc;
  gc.symbol = LiteralSym(kind, bytes);
  gc.bytes = bytes;
  RecordLiteralDataEvidence(kind, bytes, gc, "EmitLiteralData");
  return gc;
}

IRDecl EmitStringLitDecl(std::string_view content) {
  SPEC_RULE("EmitLiteral-String");

  std::vector<std::uint8_t> bytes(content.begin(), content.end());
  IRDecl decl = EmitLiteralData(LiteralKind::String, bytes);
  if (const auto* global = std::get_if<GlobalConst>(&decl)) {
    RecordStringLiteralEvidence(bytes, *global, "EmitStringLitDecl");
  }
  return decl;
}

IRDecl EmitBytesLitDecl(const std::vector<std::uint8_t>& content) {
  SPEC_RULE("EmitLiteral-Bytes");
  IRDecl decl = EmitLiteralData(LiteralKind::Bytes, content);
  if (const auto* global = std::get_if<GlobalConst>(&decl)) {
    RecordBytesLiteralEvidence(
        content,
        *global,
        "EmitBytesLitDecl",
        IRImmediateLiteralKind::Bytes);
  }
  return decl;
}

IRDecl EmitCharLitDecl(char32_t codepoint) {
  SPEC_RULE("EmitLiteral-Char");
  SPEC_RULE("rule.24.EmitLiteral-Char");

  std::vector<std::uint8_t> bytes(4);
  bytes[0] = static_cast<std::uint8_t>(codepoint & 0xFF);
  bytes[1] = static_cast<std::uint8_t>((codepoint >> 8) & 0xFF);
  bytes[2] = static_cast<std::uint8_t>((codepoint >> 16) & 0xFF);
  bytes[3] = static_cast<std::uint8_t>((codepoint >> 24) & 0xFF);
  return EmitLiteralData(LiteralKind::Char, bytes);
}

IRDecl EmitIntLitDecl(const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("EmitLiteral-Int");
  SPEC_RULE("rule.24.EmitLiteral-Int");
  return EmitLiteralData(LiteralKind::Int, bytes);
}

IRDecl EmitFloatLitDecl(const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("EmitLiteral-Float");
  SPEC_RULE("rule.24.EmitLiteral-Float");
  return EmitLiteralData(LiteralKind::Float, bytes);
}

// ============================================================================
// Section 6.12.14 Literal Reference Collection
// ============================================================================

namespace {

void CollectLiteralRefsFromIR(const IRPtr& ir,
                               std::vector<LiteralRefInfo>& out);
void CollectLiteralRefsFromValue(const IRValue& value,
                                  std::vector<LiteralRefInfo>& out);

std::vector<LiteralRefInfo> DedupLiteralRefs(std::vector<LiteralRefInfo> refs) {
  std::vector<LiteralRefInfo> unique_refs;
  std::unordered_map<std::string, std::size_t> index_by_symbol;

  for (auto& ref : refs) {
    const std::string symbol = LiteralSym(ref.kind, ref.bytes);
    const auto existing = index_by_symbol.find(symbol);
    if (existing == index_by_symbol.end()) {
      index_by_symbol.emplace(symbol, unique_refs.size());
      unique_refs.push_back(std::move(ref));
      continue;
    }

    LiteralRefInfo& stored = unique_refs[existing->second];
    if (!stored.source_literal_kind.has_value() &&
        ref.source_literal_kind.has_value()) {
      stored.source_literal_kind = ref.source_literal_kind;
    } else if (
        stored.kind == LiteralKind::Bytes &&
        ref.source_literal_kind.has_value() &&
        *ref.source_literal_kind == IRImmediateLiteralKind::String) {
      stored.source_literal_kind = ref.source_literal_kind;
    }
  }

  return unique_refs;
}

void CollectLiteralRefsFromOptionalValue(
    const std::optional<IRValue>& value,
    std::vector<LiteralRefInfo>& out) {
  if (value.has_value()) {
    CollectLiteralRefsFromValue(*value, out);
  }
}

void CollectLiteralRefsFromValue(const IRValue& value,
                                  std::vector<LiteralRefInfo>& out) {
  if (auto kind = LiteralKindOfImmediate(value); kind.has_value()) {
    out.push_back(LiteralRefInfo{
        *kind,
        value.bytes,
        SourceLiteralKindOfImmediate(value, *kind),
    });
  }
}

void CollectLiteralRefsFromIR(const IRPtr& ir,
                               std::vector<LiteralRefInfo>& out) {
  if (!ir) return;

  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;

    if constexpr (std::is_same_v<T, IRSeq>) {
      for (const auto& item : node.items) {
        CollectLiteralRefsFromIR(item, out);
      }
    } else if constexpr (std::is_same_v<T, IRCall>) {
      CollectLiteralRefsFromValue(node.callee, out);
      for (const auto& arg : node.args) {
        CollectLiteralRefsFromValue(arg, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCallVTable>) {
      CollectLiteralRefsFromValue(node.base, out);
      for (const auto& arg : node.args) {
        CollectLiteralRefsFromValue(arg, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRBindVar>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRStoreVar>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRStoreVarNoDrop>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRStoreGlobal>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRWritePlace>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRReadPtr>) {
      CollectLiteralRefsFromValue(node.ptr, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRWritePtr>) {
      CollectLiteralRefsFromValue(node.ptr, out);
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRUnaryOp>) {
      CollectLiteralRefsFromValue(node.operand, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRFence>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRBinaryOp>) {
      CollectLiteralRefsFromValue(node.lhs, out);
      CollectLiteralRefsFromValue(node.rhs, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCast>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRTransmute>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCheckIndex>) {
      CollectLiteralRefsFromValue(node.base, out);
      CollectLiteralRefsFromValue(node.index, out);
    } else if constexpr (std::is_same_v<T, IRCheckRange>) {
      CollectLiteralRefsFromValue(node.base, out);
      CollectLiteralRefsFromOptionalValue(node.range.lo, out);
      CollectLiteralRefsFromOptionalValue(node.range.hi, out);
      CollectLiteralRefsFromOptionalValue(node.range_value, out);
    } else if constexpr (std::is_same_v<T, IRCheckSliceLen>) {
      CollectLiteralRefsFromValue(node.base, out);
      CollectLiteralRefsFromOptionalValue(node.range.lo, out);
      CollectLiteralRefsFromOptionalValue(node.range.hi, out);
      CollectLiteralRefsFromOptionalValue(node.range_value, out);
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRCheckOp>) {
      CollectLiteralRefsFromValue(node.lhs, out);
      CollectLiteralRefsFromOptionalValue(node.rhs, out);
    } else if constexpr (std::is_same_v<T, IRCheckCast>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRAlloc>) {
      CollectLiteralRefsFromOptionalValue(node.region, out);
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRContextBundleBuild>) {
      CollectLiteralRefsFromValue(node.root_ctx, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRReturn>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRResult>) {
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRBreak>) {
      CollectLiteralRefsFromOptionalValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRIf>) {
      CollectLiteralRefsFromValue(node.cond, out);
      CollectLiteralRefsFromIR(node.then_ir, out);
      CollectLiteralRefsFromIR(node.else_ir, out);
      CollectLiteralRefsFromValue(node.then_value, out);
      CollectLiteralRefsFromValue(node.else_value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRLoop>) {
      CollectLiteralRefsFromIR(node.iter_ir, out);
      CollectLiteralRefsFromIR(node.cond_ir, out);
      CollectLiteralRefsFromIR(node.body_ir, out);
      CollectLiteralRefsFromIR(node.invariant_entry_ir, out);
      CollectLiteralRefsFromIR(node.invariant_backedge_ir, out);
      CollectLiteralRefsFromOptionalValue(node.iter_value, out);
      CollectLiteralRefsFromOptionalValue(node.cond_value, out);
      CollectLiteralRefsFromValue(node.body_value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRIfCase>) {
      CollectLiteralRefsFromValue(node.scrutinee, out);
      for (const auto& arm : node.arms) {
        CollectLiteralRefsFromIR(arm.body, out);
        CollectLiteralRefsFromIR(arm.cleanup_ir, out);
        CollectLiteralRefsFromValue(arm.value, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRBlock>) {
      CollectLiteralRefsFromIR(node.setup, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRRegion>) {
      CollectLiteralRefsFromValue(node.owner, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRFrame>) {
      CollectLiteralRefsFromOptionalValue(node.region, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRBranch>) {
      CollectLiteralRefsFromOptionalValue(node.cond, out);
    } else if constexpr (std::is_same_v<T, IRPhi>) {
      for (const auto& incoming : node.incoming) {
        CollectLiteralRefsFromValue(incoming.value, out);
      }
      CollectLiteralRefsFromValue(node.value, out);
    } else if constexpr (std::is_same_v<T, IRCleanupPanicCheck>) {
      CollectLiteralRefsFromIR(node.cleanup_ir, out);
    } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
      CollectLiteralRefsFromIR(node.cleanup_ir, out);
    } else if constexpr (std::is_same_v<T, IRInitPanicRaise>) {
      CollectLiteralRefsFromIR(node.cleanup_ir, out);
    } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
      CollectLiteralRefsFromIR(node.cleanup_ir, out);
    } else if constexpr (std::is_same_v<T, IRParallel>) {
      CollectLiteralRefsFromValue(node.domain, out);
      CollectLiteralRefsFromOptionalValue(node.cancel_token, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpawn>) {
      CollectLiteralRefsFromIR(node.captured_env, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.body_result, out);
      CollectLiteralRefsFromValue(node.result, out);
      CollectLiteralRefsFromValue(node.env_ptr, out);
      CollectLiteralRefsFromValue(node.env_size, out);
      CollectLiteralRefsFromValue(node.body_fn, out);
      CollectLiteralRefsFromValue(node.result_size, out);
      CollectLiteralRefsFromOptionalValue(node.affinity_mask, out);
      CollectLiteralRefsFromOptionalValue(node.priority, out);
    } else if constexpr (std::is_same_v<T, IRWait>) {
      CollectLiteralRefsFromValue(node.handle, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCancelCreate>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCancelRequest>) {
      CollectLiteralRefsFromValue(node.token, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCancelWait>) {
      CollectLiteralRefsFromValue(node.token, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRCancelCheck>) {
      CollectLiteralRefsFromValue(node.token, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRGpuBarrier>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRDispatch>) {
      CollectLiteralRefsFromValue(node.range, out);
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.body_result, out);
      CollectLiteralRefsFromIR(node.captured_env, out);
      CollectLiteralRefsFromValue(node.env_ptr, out);
      CollectLiteralRefsFromValue(node.body_fn, out);
      CollectLiteralRefsFromValue(node.elem_size, out);
      CollectLiteralRefsFromValue(node.result_size, out);
      CollectLiteralRefsFromValue(node.result_ptr, out);
      CollectLiteralRefsFromOptionalValue(node.reduce_fn, out);
      CollectLiteralRefsFromValue(node.result, out);
      CollectLiteralRefsFromOptionalValue(node.chunk_size, out);
      CollectLiteralRefsFromValue(node.workgroup_size, out);
    } else if constexpr (std::is_same_v<T, IRYield>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
      CollectLiteralRefsFromValue(node.keys_record, out);
    } else if constexpr (std::is_same_v<T, IRYieldFrom>) {
      CollectLiteralRefsFromValue(node.source, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecSnapshot>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecValidate>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecCommit>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecRetry>) {
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecFallback>) {
      CollectLiteralRefsFromIR(node.body, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSpecLoop>) {
      CollectLiteralRefsFromIR(node.snapshot_ir, out);
      CollectLiteralRefsFromIR(node.body_ir, out);
      CollectLiteralRefsFromIR(node.validate_ir, out);
      CollectLiteralRefsFromIR(node.commit_ir, out);
      CollectLiteralRefsFromIR(node.retry_ir, out);
      CollectLiteralRefsFromIR(node.fallback_ir, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRSync>) {
      CollectLiteralRefsFromValue(node.async_value, out);
      CollectLiteralRefsFromOptionalValue(node.runtime_receiver, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
      for (const auto& arm : node.arms) {
        CollectLiteralRefsFromIR(arm.async_ir, out);
        CollectLiteralRefsFromValue(arm.async_value, out);
        CollectLiteralRefsFromValue(arm.match_value, out);
        CollectLiteralRefsFromIR(arm.handler_ir, out);
        CollectLiteralRefsFromValue(arm.handler_result, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRRaceYield>) {
      for (const auto& arm : node.arms) {
        CollectLiteralRefsFromIR(arm.async_ir, out);
        CollectLiteralRefsFromValue(arm.async_value, out);
        CollectLiteralRefsFromValue(arm.match_value, out);
        CollectLiteralRefsFromIR(arm.handler_ir, out);
        CollectLiteralRefsFromValue(arm.handler_result, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRAll>) {
      for (const auto& async_ir : node.async_irs) {
        CollectLiteralRefsFromIR(async_ir, out);
      }
      for (const auto& async_value : node.async_values) {
        CollectLiteralRefsFromValue(async_value, out);
      }
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRAsyncComplete>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    } else if constexpr (std::is_same_v<T, IRAsyncFail>) {
      CollectLiteralRefsFromValue(node.value, out);
      CollectLiteralRefsFromValue(node.result, out);
    }
    // Other IR types don't contain literals directly
  }, ir->node);
}

}  // namespace

std::vector<LiteralRefInfo> LiteralRefs(const IRPtr& ir) {
  std::vector<LiteralRefInfo> refs;
  CollectLiteralRefsFromIR(ir, refs);
  return DedupLiteralRefs(std::move(refs));
}

std::vector<LiteralRefInfo> LiteralRefs(const IRDecls& decls) {
  std::vector<LiteralRefInfo> refs;

  for (const auto& decl : decls) {
    if (const auto* proc = std::get_if<ProcIR>(&decl)) {
      CollectLiteralRefsFromIR(proc->body, refs);
    }
  }

  return DedupLiteralRefs(std::move(refs));
}

bool LiteralRef(const IRPtr& ir,
                LiteralKind kind,
                const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("LiteralRef");
  const auto refs = RefSyms(ir);
  const std::string symbol = LiteralSym(kind, bytes);
  return std::find(refs.begin(), refs.end(), symbol) != refs.end();
}

bool LiteralRef(const IRDecls& decls,
                LiteralKind kind,
                const std::vector<std::uint8_t>& bytes) {
  SPEC_RULE("LiteralRef");
  const auto refs = RefSyms(decls);
  const std::string symbol = LiteralSym(kind, bytes);
  return std::find(refs.begin(), refs.end(), symbol) != refs.end();
}

// ============================================================================
// Section 6.12.14 Literal Type Inference
// ============================================================================

analysis::TypeRef StaticTypeForConst(const GlobalConst& global,
                                      const LowerCtx* ctx) {
  if (ctx) {
    auto type = ctx->LookupStaticType(global.symbol);
    if (type) {
      return type;
    }
  }
  if (IsLiteralSymbol(global.symbol)) {
    return analysis::MakeTypeArray(analysis::MakeTypePrim("u8"), global.bytes.size());
  }
  return nullptr;
}

// ============================================================================
// Section 6.12.14 Literal Deduplication
// ============================================================================

std::vector<IRDecl> UniqueLiterals(const std::vector<LiteralRefInfo>& lits) {
  SPEC_RULE("UniqueEmits-Literal");

  std::unordered_set<std::string> seen_symbols;
  std::vector<IRDecl> unique_decls;

  for (const auto& lit : lits) {
    const LiteralKind kind = lit.kind;
    const std::vector<std::uint8_t>& bytes = lit.bytes;
    std::string sym = LiteralSym(kind, bytes);
    if (seen_symbols.find(sym) == seen_symbols.end()) {
      seen_symbols.insert(sym);
      IRDecl decl =
          EmitLiteralData(kind, bytes);
      if (const auto* global = std::get_if<GlobalConst>(&decl)) {
        if (kind == LiteralKind::String) {
          RecordStringLiteralEvidence(bytes, *global, "UniqueLiterals");
        } else if (kind == LiteralKind::Bytes) {
          RecordBytesLiteralEvidence(
              bytes,
              *global,
              "UniqueLiterals",
              lit.source_literal_kind);
        }
      }
      unique_decls.push_back(std::move(decl));
    }
  }

  return unique_decls;
}

// ============================================================================
// Section 6.12.14 String/Bytes View Construction
// ============================================================================

StringViewLayout GetStringViewLayout(
    const ::ultraviolet::analysis::layout::LayoutEnv& env) {
  // string@View = { ptr: *imm u8, len: usize }
  const std::uint64_t ptr_size = ::ultraviolet::analysis::layout::PtrSize(env);
  StringViewLayout layout;
  layout.ptr_offset = 0;
  layout.len_offset = ptr_size;
  layout.total_size = ptr_size * 2;
  return layout;
}

StringViewLayout GetStringViewLayout(project::TargetProfile target_profile) {
  return GetStringViewLayout(
      ::ultraviolet::analysis::layout::LayoutEnvOf(target_profile));
}

StringViewLayout GetStringViewLayout() {
  return GetStringViewLayout(project::TargetProfile::X86_64SysV);
}

StringViewLayout GetBytesViewLayout(
    const ::ultraviolet::analysis::layout::LayoutEnv& env) {
  // bytes@View = { ptr: *imm u8, len: usize }
  // Same layout as string@View
  return GetStringViewLayout(env);
}

StringViewLayout GetBytesViewLayout(project::TargetProfile target_profile) {
  return GetStringViewLayout(target_profile);
}

StringViewLayout GetBytesViewLayout() {
  return GetBytesViewLayout(project::TargetProfile::X86_64SysV);
}

IRPtr EmitStringViewIR(const std::string& literal_sym,
                       std::size_t length,
                       const IRValue& result,
                       LowerCtx& ctx) {
  SPEC_RULE("EmitStringViewIR");

  // Create a string@View struct from the literal symbol
  // This involves:
  // 1. Getting the address of the global literal
  // 2. Creating a struct with { ptr, len }

  std::vector<IRPtr> parts;

  // Get address of literal
  IRValue lit_addr = ctx.FreshTempValue("lit_addr");
  IRAddrOf addr_of;
  addr_of.place.repr = literal_sym;
  addr_of.result = lit_addr;
  parts.push_back(MakeIR(std::move(addr_of)));

  // Create length immediate
  IRValue len_val;
  len_val.kind = IRValue::Kind::Immediate;
  len_val.name = std::to_string(length);
  const auto env = ::ultraviolet::analysis::layout::LayoutEnvOf(
      ctx.target_profile.value_or(project::TargetProfile::X86_64SysV));
  const std::uint64_t ptr_size = ::ultraviolet::analysis::layout::PtrSize(env);
  len_val.bytes.resize(static_cast<std::size_t>(ptr_size));
  const auto encoded_length = static_cast<std::uint64_t>(length);
  for (std::uint64_t i = 0; i < ptr_size; ++i) {
    len_val.bytes[i] =
        i < sizeof(encoded_length)
            ? static_cast<std::uint8_t>((encoded_length >> (i * 8)) & 0xFF)
            : 0;
  }

  // The actual struct construction would be handled by the LLVM emitter
  // based on the string@View type layout

  return SeqIR(std::move(parts));
}

IRPtr EmitBytesViewIR(const std::string& literal_sym,
                      std::size_t length,
                      const IRValue& result,
                      LowerCtx& ctx) {
  SPEC_RULE("EmitBytesViewIR");
  // Same as string view construction
  return EmitStringViewIR(literal_sym, length, result, ctx);
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorLiteralEmitRules() {
  // Section 6.12.14 Literal Emission
  SPEC_RULE("EmitLiteralData-Decl");
  SPEC_RULE("EmitLiteralData-Bytes");
  SPEC_RULE("EmitLiteral-String");
  SPEC_RULE("EmitLiteral-Bytes");
  SPEC_RULE("EmitLiteral-Char");
  SPEC_RULE("EmitLiteral-Int");
  SPEC_RULE("EmitLiteral-Float");
  SPEC_RULE("EmitLiteral-Err");
  SPEC_RULE("UniqueEmits-Literal");
  SPEC_RULE("LiteralSym");
  SPEC_RULE("EmitStringViewIR");
  SPEC_RULE("EmitBytesViewIR");

  // Section 6.3.1 Mangle-Literal
  SPEC_RULE("Mangle-Literal");

  // Section 6.3.1 Linkage-LiteralData
  SPEC_RULE("Linkage-LiteralData");
}

}  // namespace ultraviolet::codegen
