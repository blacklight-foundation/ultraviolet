// =============================================================================
// MIGRATION MAPPING: literals.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.10: Literal Expressions (lines 9174-9285)
//   - IntTypes, FloatTypes sets (lines 9176-9177)
//   - FloatFormat, FloatBitWidth definitions (lines 9178-9179)
//   - IEEE754 encoding definitions (lines 9180-9186)
//   - DefaultInt = i32, DefaultFloat = f32 (lines 9187-9188)
//   - IntValue, FloatValue functions (line 9189)
//   - StripIntSuffix, StripFloatSuffix (lines 9193-9198)
//   - InRange, RangeOf definitions (lines 9204-9208)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/literals.cpp
//   Lines 1-449 (complete file)
//
// KEY CONTENT TO MIGRATE:
//   SPEC DEFINITIONS (lines 17-29):
//   - SpecDefsLiterals() references "5.2.10"
//
//   CONSTANTS (lines 31-46):
//   - kPointerSizeBytes = 8, kPointerSizeBits = 64
//   - kIntSuffixes array: i128, u128, isize, usize, i64, u64, i32, u32, i16, u16, i8, u8
//   - kFloatSuffixes array: f16, f32, f64, f
//   - kIntTypes, kFloatTypes arrays
//
//   SUFFIX PARSING (lines 48-91):
//   - EndsWith() helper
//   - MatchSuffix() - find matching suffix
//   - IntSuffix() - extract integer suffix from token
//   - FloatSuffix() - extract float suffix from token
//   - IntCore() - strip suffix from integer literal
//
//   INTEGER PARSING (lines 93-183):
//   - DigitValue() - single digit value for any base
//   - ParseIntCore() - full integer parsing with UInt128
//     * Handles 0x, 0o, 0b prefixes
//     * Handles underscore separators
//     * Overflow detection
//   - IntValue() - parse token to UInt128
//
//   TYPE CHECKING (lines 185-273):
//   - IsIntTypeName(), IsFloatTypeName() - type name validation
//   - IntWidthOf() - bit width for type name
//   - IsUnsignedIntType(), IsSignedIntType()
//   - InRangeUnsigned(), InRangeSigned() - value range checks
//   - InRangeInt() - combined range check
//
//   LITERAL TYPING (lines 277-352):
//   - TypeLiteralExpr() main function
//     * IntLiteral: suffix or default i32 (T-Int-Literal-Suffix, T-Int-Literal-Default)
//     * FloatLiteral: suffix required, 'f' defaults to f32 (T-Float-Literal-Explicit, T-Float-Literal-Infer)
//     * BoolLiteral: bool type (T-Bool-Literal)
//     * CharLiteral: char type (T-Char-Literal)
//     * StringLiteral: string@View (T-String-Literal)
//
//   LITERAL CHECKING (lines 354-446):
//   - NullLiteralExpected() - check if null is valid for type
//   - CheckLiteralExpr() - check literal against expected type
//     * IntLiteral: check range for target type (Chk-Int-Literal)
//     * FloatLiteral: explicit suffix must match, 'f' accepts any (Chk-Float-Literal-*)
//     * NullLiteral: must expect raw pointer (Chk-Null-Literal)
//
// DEPENDENCIES:
//   - ScopeContext for context (currently unused)
//   - syntax::LiteralExpr, syntax::Token
//   - core::UInt128 for large integer support
//   - TypeRef and MakeType* constructors
//   - TypePrim, TypePerm, TypeRefine, TypeRawPtr type nodes
//
// REFACTORING NOTES:
//   1. UInt128 dependency is for i128/u128 literal support
//   2. Float suffix 'f' is special - accepts any float type from context
//   3. String literals always produce string@View (borrowed)
//   4. NullLiteral requires type context (raw pointer expected)
//   5. Consider separating:
//      - literal_parse.cpp (suffix extraction, value parsing)
//      - literal_type.cpp (type inference and checking)
//
// SPEC RULES IMPLEMENTED:
//   - T-Int-Literal-Suffix, T-Int-Literal-Default
//   - T-Float-Literal-Explicit, T-Float-Literal-Infer
//   - T-Bool-Literal, T-Char-Literal, T-String-Literal
//   - Chk-Int-Literal, Chk-Float-Literal-Explicit, Chk-Float-Literal-Infer
//   - Chk-Float-Literal-Mismatch-Err (E-TYP-1531)
//   - Chk-Null-Literal, Chk-Null-Literal-Err
//
// =============================================================================

#include "04_analysis/typing/literals.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/int128.h"
#include "00_core/numeric_literals.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsLiterals() {
  SPEC_DEF("IntTypes", "5.2.10");
  SPEC_DEF("FloatTypes", "5.2.10");
  SPEC_DEF("DefaultInt", "5.2.10");
  SPEC_DEF("DefaultFloat", "5.2.10");
  SPEC_DEF("StripIntSuffix", "5.2.10");
  SPEC_DEF("StripFloatSuffix", "5.2.10");
  SPEC_DEF("IntValue", "5.2.10");
  SPEC_DEF("FloatValue", "5.2.10");
  SPEC_DEF("InRange", "5.2.10");
  SPEC_DEF("RangeOf", "5.2.10");
  SPEC_DEF("NullLiteralExpected", "5.2.10");
}

static constexpr unsigned kPointerSizeBytes = 8;
static constexpr unsigned kPointerSizeBits = 8 * kPointerSizeBytes;

static constexpr std::array<std::string_view, 12> kIntSuffixes = {
    "i128", "u128", "isize", "usize", "i64", "u64",
    "i32",  "u32",  "i16",  "u16",  "i8",  "u8"};

static constexpr std::array<std::string_view, 4> kFloatSuffixes = {
    "f16", "f32", "f64", "f"};

static constexpr std::array<std::string_view, 12> kIntTypes = {
    "i8",   "i16",  "i32",  "i64",  "i128", "u8",
    "u16",  "u32",  "u64",  "u128", "isize", "usize"};

static constexpr std::array<std::string_view, 3> kFloatTypes = {
    "f16", "f32", "f64"};

static bool EndsWith(std::string_view value, std::string_view suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.substr(value.size() - suffix.size()) == suffix;
}

static bool StartsWith(std::string_view value, std::string_view prefix) {
  if (prefix.size() > value.size()) {
    return false;
  }
  return value.substr(0, prefix.size()) == prefix;
}

static bool IsDecDigitChar(char c) {
  return c >= '0' && c <= '9';
}

static bool IsHexDigitChar(char c) {
  return IsDecDigitChar(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsOctDigitChar(char c) {
  return c >= '0' && c <= '7';
}

static bool IsBinDigitChar(char c) {
  return c == '0' || c == '1';
}

static bool MatchDigitRunSyntax(std::string_view text, bool (*pred)(char)) {
  bool saw_digit = false;
  for (char c : text) {
    if (c == '_') {
      continue;
    }
    if (!pred(c)) {
      return false;
    }
    saw_digit = true;
  }
  return saw_digit;
}

static bool IntegerUnderscoreSyntaxOk(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  if (text.front() == '_' || text.back() == '_') {
    return false;
  }
  if (StartsWith(text, "0x_") || StartsWith(text, "0o_") ||
      StartsWith(text, "0b_")) {
    return false;
  }
  return true;
}

static std::optional<std::string_view> MatchSuffix(
    std::string_view lexeme,
    std::span<const std::string_view> suffixes) {
  for (const auto suffix : suffixes) {
    if (EndsWith(lexeme, suffix)) {
      const std::size_t core_len = lexeme.size() - suffix.size();
      if (core_len == 0) {
        continue;
      }
      return suffix;
    }
  }
  return std::nullopt;
}

static std::optional<std::string_view> IntSuffix(const ast::Token& lit) {
  if (lit.kind != lexer::TokenKind::IntLiteral) {
    return std::nullopt;
  }
  return MatchSuffix(lit.lexeme, kIntSuffixes);
}

static bool IsIntegerLiteralSyntax(std::string_view lexeme) {
  std::string_view core = lexeme;
  if (const auto suffix = MatchSuffix(lexeme, kIntSuffixes)) {
    core = lexeme.substr(0, lexeme.size() - suffix->size());
  }
  if (!IntegerUnderscoreSyntaxOk(core)) {
    return false;
  }
  if (core.size() >= 2 && core[0] == '0') {
    if (core[1] == 'x') {
      return MatchDigitRunSyntax(core.substr(2), IsHexDigitChar);
    }
    if (core[1] == 'o') {
      return MatchDigitRunSyntax(core.substr(2), IsOctDigitChar);
    }
    if (core[1] == 'b') {
      return MatchDigitRunSyntax(core.substr(2), IsBinDigitChar);
    }
  }
  return MatchDigitRunSyntax(core, IsDecDigitChar);
}

static std::optional<std::string_view> FloatSuffix(const ast::Token& lit) {
  if (lit.kind != lexer::TokenKind::FloatLiteral) {
    return std::nullopt;
  }
  return MatchSuffix(lit.lexeme, kFloatSuffixes);
}

static std::optional<core::UInt128> IntValue(const ast::Token& lit) {
  if (lit.kind != lexer::TokenKind::IntLiteral) {
    return std::nullopt;
  }
  const std::string_view core_text = core::StripIntSuffix(lit.lexeme);
  return core::ParseIntCore(core_text);
}

static bool IsIntTypeName(std::string_view name) {
  for (const auto& t : kIntTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static bool IsFloatTypeName(std::string_view name) {
  for (const auto& t : kFloatTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static std::optional<unsigned> IntWidthOf(std::string_view name) {
  if (name == "i8" || name == "u8") {
    return 8;
  }
  if (name == "i16" || name == "u16") {
    return 16;
  }
  if (name == "i32" || name == "u32") {
    return 32;
  }
  if (name == "i64" || name == "u64") {
    return 64;
  }
  if (name == "i128" || name == "u128") {
    return 128;
  }
  if (name == "isize" || name == "usize") {
    return kPointerSizeBits;
  }
  return std::nullopt;
}

static bool IsUnsignedIntType(std::string_view name) {
  return name == "u8" || name == "u16" || name == "u32" ||
         name == "u64" || name == "u128" || name == "usize";
}

static bool IsSignedIntType(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" ||
         name == "i64" || name == "i128" || name == "isize";
}

static bool InRangeUnsigned(core::UInt128 value, unsigned width) {
  if (width >= 128) {
    return true;
  }
  const core::UInt128 one = core::UInt128FromU64(1);
  const core::UInt128 max =
      core::UInt128Sub(core::UInt128ShiftLeft(one, width), one);
  return core::UInt128LessOrEqual(value, max);
}

static bool InRangeSigned(core::UInt128 value, unsigned width) {
  if (width == 0) {
    return false;
  }
  if (width >= 128) {
    const core::UInt128 one = core::UInt128FromU64(1);
    const core::UInt128 max =
        core::UInt128Sub(core::UInt128ShiftLeft(one, 127), one);
    return core::UInt128LessOrEqual(value, max);
  }
  const core::UInt128 one = core::UInt128FromU64(1);
  const core::UInt128 max =
      core::UInt128Sub(core::UInt128ShiftLeft(one, width - 1), one);
  return core::UInt128LessOrEqual(value, max);
}

static bool InRangeInt(core::UInt128 value, std::string_view name) {
  const auto width = IntWidthOf(name);
  if (!width.has_value()) {
    return false;
  }
  if (IsUnsignedIntType(name)) {
    return InRangeUnsigned(value, *width);
  }
  if (IsSignedIntType(name)) {
    return InRangeSigned(value, *width);
  }
  return false;
}

}  // namespace

ExprTypeResult TypeLiteralExpr(const ScopeContext& ctx,
                               const ast::LiteralExpr& expr) {
  (void)ctx;
  SpecDefsLiterals();
  ExprTypeResult result;
  const auto& lit = expr.literal;
  switch (lit.kind) {
    case lexer::TokenKind::IntLiteral: {
      const auto value = IntValue(lit);
      if (!value.has_value()) {
        if (!IsIntegerLiteralSyntax(lit.lexeme)) {
          result.diag_id = "E-SRC-0304";
          return result;
        }
        return result;
      }
      if (const auto suffix = IntSuffix(lit)) {
        if (!InRangeInt(*value, *suffix)) {
          return result;
        }
        SPEC_RULE("T-Int-Literal-Suffix");
        result.ok = true;
        result.type = MakeTypePrim(std::string(*suffix));
        return result;
      }
      if (!InRangeInt(*value, "i32")) {
        return result;
      }
      SPEC_RULE("T-Int-Literal-Default");
      result.ok = true;
      result.type = MakeTypePrim("i32");
      return result;
    }
    case lexer::TokenKind::FloatLiteral: {
      if (const auto suffix = FloatSuffix(lit)) {
        // Bare 'f' suffix defaults to f32
        if (*suffix == "f") {
          SPEC_RULE("T-Float-Literal-Infer");
          result.ok = true;
          result.type = MakeTypePrim("f32");
          return result;
        }
        // Explicit width suffix (f16, f32, f64)
        SPEC_RULE("T-Float-Literal-Explicit");
        result.ok = true;
        result.type = MakeTypePrim(std::string(*suffix));
        return result;
      }
      // Unsuffixed decimal floats default to f32 in synthesis contexts.
      SPEC_RULE("T-Float-Literal-Infer");
      result.ok = true;
      result.type = MakeTypePrim("f32");
      return result;
    }
    case lexer::TokenKind::BoolLiteral:
      SPEC_RULE("T-Bool-Literal");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    case lexer::TokenKind::CharLiteral:
      SPEC_RULE("T-Char-Literal");
      result.ok = true;
      result.type = MakeTypePrim("char");
      return result;
    case lexer::TokenKind::StringLiteral:
      SPEC_RULE("T-String-Literal");
      result.ok = true;
      result.type = MakeTypeString(StringState::View);
      return result;
    case lexer::TokenKind::NullLiteral:
    case lexer::TokenKind::Identifier:
    case lexer::TokenKind::Keyword:
    case lexer::TokenKind::Operator:
    case lexer::TokenKind::Punctuator:
    case lexer::TokenKind::Newline:
    case lexer::TokenKind::Unknown:
      break;
  }
  return result;
}

bool NullLiteralExpected(const TypeRef& expected) {
  SpecDefsLiterals();
  if (!expected) {
    return false;
  }
  TypeRef cur = expected;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur && std::holds_alternative<TypeRawPtr>(cur->node);
}

LiteralCheckResult CheckLiteralExpr(const ScopeContext& ctx,
                                    const ast::LiteralExpr& expr,
                                    const TypeRef& expected) {
  (void)ctx;
  SpecDefsLiterals();
  LiteralCheckResult result;
  if (!expected) {
    return result;
  }
  TypeRef base = expected;
  while (base) {
    if (const auto* perm = std::get_if<TypePerm>(&base->node)) {
      base = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&base->node)) {
      base = refine->base;
      continue;
    }
    break;
  }
  if (!base) {
    return result;
  }
  const auto& lit = expr.literal;
  if (lit.kind == lexer::TokenKind::IntLiteral) {
    const auto* prim = std::get_if<TypePrim>(&base->node);
    if (!prim || !IsIntTypeName(prim->name)) {
      return result;
    }
    const auto value = IntValue(lit);
    if (!value.has_value()) {
      if (!IsIntegerLiteralSyntax(lit.lexeme)) {
        result.diag_id = "E-SRC-0304";
      }
      return result;
    }
    if (!InRangeInt(*value, prim->name)) {
      return result;
    }
    SPEC_RULE("Chk-Int-Literal");
    result.ok = true;
    return result;
  }
  if (lit.kind == lexer::TokenKind::FloatLiteral) {
    const auto* prim = std::get_if<TypePrim>(&base->node);
    if (!prim || !IsFloatTypeName(prim->name)) {
      return result;
    }
    const auto suffix = FloatSuffix(lit);
    // Bare 'f' and unsuffixed decimal float literals accept any expected
    // float type; declared/expected type takes precedence over defaulting.
    if (!suffix.has_value() || *suffix == "f") {
      SPEC_RULE("Chk-Float-Literal-Infer");
      result.ok = true;
      return result;
    }
    // Explicit suffix must match expected type
    if (*suffix == prim->name) {
      SPEC_RULE("Chk-Float-Literal-Explicit");
      result.ok = true;
      return result;
    }
    // Explicit suffix mismatch - error
    SPEC_RULE("Chk-Float-Literal-Mismatch-Err");
    result.diag_id = "E-TYP-1531";
    return result;
  }
  if (lit.kind == lexer::TokenKind::BoolLiteral) {
    const auto* prim = std::get_if<TypePrim>(&base->node);
    if (!prim || prim->name != "bool") {
      return result;
    }
    result.ok = true;
    return result;
  }
  if (lit.kind == lexer::TokenKind::CharLiteral) {
    const auto* prim = std::get_if<TypePrim>(&base->node);
    if (!prim || prim->name != "char") {
      return result;
    }
    result.ok = true;
    return result;
  }
  if (lit.kind == lexer::TokenKind::StringLiteral) {
    const auto* str = std::get_if<TypeString>(&base->node);
    if (!str || !str->state.has_value() ||
        *str->state != StringState::View) {
      return result;
    }
    result.ok = true;
    return result;
  }
  if (lit.kind == lexer::TokenKind::NullLiteral) {
    if (NullLiteralExpected(expected)) {
      SPEC_RULE("Chk-Null-Literal");
      result.ok = true;
      return result;
    }
    SPEC_RULE("Chk-Null-Literal-Err");
    result.diag_id = "NullLiteral-Infer-Err";
    return result;
  }
  return result;
}

}  // namespace ultraviolet::analysis
