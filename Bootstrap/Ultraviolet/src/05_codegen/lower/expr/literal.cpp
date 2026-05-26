// =============================================================================
// MIGRATION MAPPING: expr/literal.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Line 16048-16051: (Lower-Expr-Literal)
//     T = ExprType(Literal(l))    LiteralValue(l, T) = v
//     --------------------------------
//     Gamma |- LowerExpr(Literal(l)) => <epsilon, v>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 25-37: StripIntSuffix helper
//   - Lines 39-85: ParseIntLiteralLexeme - parses integer literals (binary, octal, decimal, hex)
//   - Lines 96-108: EncodeU64BE - encodes integers as big-endian bytes
//   - Lines 238-253: LEBytesU64 - encodes integers as little-endian bytes
//   - Literal lowering produces IRValue with immediate bytes
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRValue, IRValue::Kind::Immediate)
//   - ultraviolet/src/04_analysis/types/types.h (TypeRef, TypePrim)
//
// REFACTORING NOTES:
//   1. Integer literals support suffixes: i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize
//   2. Float literals require suffix: f, f16, f32, f64
//   3. Integer prefixes: 0b (binary), 0o (octal), 0x (hex)
//   4. Underscores allowed in numeric literals for readability
//   5. Character literals use char escape processing
//   6. String literals produce string@View values
//   7. Boolean literals produce bool immediates
//
// LITERAL VALUE ENCODING:
//   - Integers: Little-endian byte representation
//   - Floats: IEEE 754 encoding (f16, f32, f64)
//   - Bool: 1 byte (0 or 1)
//   - Char: 4-byte UTF-32 codepoint
//
// =============================================================================

#include "05_codegen/lower/expr/literal.h"
#include "04_analysis/layout/layout.h"
#include "00_core/assert_spec.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ultraviolet::codegen {

namespace {

// Integer suffix table (ordered longest-first to avoid prefix matching issues)
constexpr std::array<std::string_view, 12> kIntSuffixes = {
    "i128", "u128", "isize", "usize", "i64", "u64",
    "i32",  "u32",  "i16",  "u16",  "i8",  "u8"
};

// Strip integer suffix from a literal lexeme
std::string StripIntSuffix(std::string_view text) {
    for (const auto& suffix : kIntSuffixes) {
        if (text.size() >= suffix.size() &&
            text.substr(text.size() - suffix.size()) == suffix) {
            return std::string(text.substr(0, text.size() - suffix.size()));
        }
    }
    return std::string(text);
}

// Check if character is a valid digit for the given base
bool IsDigit(char c, unsigned base) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0') < base;
    }
    if (base > 10) {
        if (c >= 'a' && c <= 'f') {
            return true;
        }
        if (c >= 'A' && c <= 'F') {
            return true;
        }
    }
    return false;
}

// Get digit value for given character
unsigned DigitValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned>(10 + c - 'A');
    }
    return 0;
}

// Parse an integer literal lexeme (handles 0b, 0o, 0x prefixes and underscores)
std::optional<std::uint64_t> ParseIntLiteralLexeme(const std::string& lexeme) {
    std::string text = StripIntSuffix(lexeme);

    // Handle binary prefix
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        text.erase(0, 2);
        if (text.empty()) {
            return std::nullopt;
        }
        std::uint64_t out = 0;
        for (char c : text) {
            if (c == '_') {
                continue;
            }
            if (c != '0' && c != '1') {
                return std::nullopt;
            }
            out = (out << 1) | static_cast<std::uint64_t>(c == '1');
        }
        return out;
    }

    // Handle octal prefix
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
        text.erase(0, 2);
        if (text.empty()) {
            return std::nullopt;
        }
        std::uint64_t out = 0;
        for (char c : text) {
            if (c == '_') {
                continue;
            }
            if (c < '0' || c > '7') {
                return std::nullopt;
            }
            out = (out << 3) | static_cast<std::uint64_t>(c - '0');
        }
        return out;
    }

    // Handle hex prefix
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
        if (text.empty()) {
            return std::nullopt;
        }
        std::uint64_t out = 0;
        for (char c : text) {
            if (c == '_') {
                continue;
            }
            if (!IsDigit(c, 16)) {
                return std::nullopt;
            }
            out = (out << 4) | DigitValue(c);
        }
        return out;
    }

    // Decimal: remove underscores and parse
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char c : text) {
        if (c != '_') {
            cleaned.push_back(c);
        }
    }
    if (cleaned.empty()) {
        return std::nullopt;
    }

    try {
        size_t idx = 0;
        std::uint64_t out = std::stoull(cleaned, &idx, 10);
        if (idx != cleaned.size()) {
            return std::nullopt;
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

// Encode a 64-bit value as little-endian bytes (variable length, non-zero)
std::vector<std::uint8_t> EncodeU64LE(std::uint64_t value) {
    std::vector<std::uint8_t> bytes;
    if (value == 0) {
        bytes.push_back(0);
        return bytes;
    }
    while (value > 0) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
        value >>= 8;
    }
    return bytes;
}

// Encode a 64-bit value as n little-endian bytes
std::vector<std::uint8_t> LEBytesU64(std::uint64_t value, std::size_t n) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
    return bytes;
}

}  // namespace

// =============================================================================
// LowerLiteral - Lower a literal expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Literal)
//   T = ExprType(Literal(l))    LiteralValue(l, T) = v
//   Gamma |- LowerExpr(Literal(l)) => <epsilon, v>
//
// Literal expressions produce an immediate IRValue with no accompanying IR.
// The value's bytes field contains the encoded literal representation.
// =============================================================================

LowerResult LowerLiteral(const ast::Expr& expr,
                         const ast::LiteralExpr& lit,
                         LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Literal");

    IRValue value;
    value.kind = IRValue::Kind::Immediate;
    value.name = lit.literal.lexeme;
    value.literal_id = ++(*ctx.temp_counter);

    // String literals: decode escape sequences to UTF-8 bytes
    if (lit.literal.kind == lexer::TokenKind::StringLiteral) {
        SPEC_RULE("StringLiteralVal");
        if (auto decoded = ::ultraviolet::analysis::layout::DecodeStringLiteralBytes(lit.literal.lexeme)) {
            value.bytes = std::move(*decoded);
        } else {
            ctx.ReportCodegenFailure();
        }
    }

    // If we have type information, use EncodeConst for proper encoding
    if (ctx.expr_type) {
        const auto lit_type = ctx.expr_type(expr);
        if (lit_type) {
            if (auto bytes = ::ultraviolet::analysis::layout::EncodeConst(lit_type, lit.literal)) {
                value.bytes = std::move(*bytes);
            }
            ctx.RegisterValueType(value, lit_type);
        }
    }

    // Fallback encoding for integers without type information
    if (value.bytes.empty() && lit.literal.kind == lexer::TokenKind::IntLiteral) {
        if (auto parsed = ParseIntLiteralLexeme(lit.literal.lexeme)) {
            value.bytes = EncodeU64LE(*parsed);
        }
    }
    // Fallback encoding for floats without type information.
    // Preserve C0 float suffix defaults: `f`/unspecified => f32.
    else if (value.bytes.empty() && lit.literal.kind == lexer::TokenKind::FloatLiteral) {
        auto ends_with = [](std::string_view text, std::string_view suffix) -> bool {
            return text.size() >= suffix.size() &&
                   text.substr(text.size() - suffix.size()) == suffix;
        };

        analysis::TypeRef fallback_float_type = analysis::MakeTypePrim("f32");
        if (ends_with(lit.literal.lexeme, "f16")) {
            fallback_float_type = analysis::MakeTypePrim("f16");
        } else if (ends_with(lit.literal.lexeme, "f64")) {
            fallback_float_type = analysis::MakeTypePrim("f64");
        } else if (ends_with(lit.literal.lexeme, "f32") ||
                   ends_with(lit.literal.lexeme, "f")) {
            fallback_float_type = analysis::MakeTypePrim("f32");
        }

        if (auto bytes = ::ultraviolet::analysis::layout::EncodeConst(fallback_float_type, lit.literal)) {
            value.bytes = std::move(*bytes);
            // Hosted/synthesized lowering paths may not provide ctx.expr_type.
            // Preserve fallback float type so downstream unary lowering and LLVM
            // emission still classify the literal as floating-point.
            ctx.RegisterValueType(value, fallback_float_type);
        }
    }
    // Fallback encoding for booleans
    else if (value.bytes.empty() && lit.literal.kind == lexer::TokenKind::BoolLiteral) {
        value.bytes = {static_cast<std::uint8_t>(lit.literal.lexeme == "true" ? 1 : 0)};
    }
    // Fallback encoding for null
    else if (value.bytes.empty() && lit.literal.kind == lexer::TokenKind::NullLiteral) {
        value.bytes = {0};
    }

    return LowerResult{EmptyIR(), value};
}

}  // namespace ultraviolet::codegen
