#pragma once

// ============================================================================
// §6.12.14 Literal Data Emission
// ============================================================================
// This header declares the literal emission functionality for Ultraviolet codegen.
// This includes emission of:
// - String literals (string@View)
// - Bytes literals (bytes@View)
// - Numeric literals (char, int, float)
// - Array literals
//
// LiteralEmitJudg = {EmitLiteralData(kind, bytes) => IRDecl,
//                    EmitStringLit(lit) => sym, EmitBytesLit(lit) => sym}
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"

namespace ultraviolet::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// ============================================================================
// §6.12.14 Literal Kind Classification
// ============================================================================

// Literal data kinds for emission
enum class LiteralKind {
  String,  // string@View content
  Bytes,   // bytes@View content
  Char,    // char literal
  Int,     // integer literal
  Float,   // floating-point literal
  Array,   // array literal
};

// Convert literal kind to string for mangling
std::string_view LiteralKindToString(LiteralKind kind);

// ============================================================================
// §6.12.14 Literal Symbol Generation
// ============================================================================

// (LiteralSym): Compute the mangled symbol for a literal constant
// LiteralSym(kind, bytes) = Mangle(LiteralData(kind, bytes))
std::string LiteralSym(LiteralKind kind, const std::vector<std::uint8_t>& bytes);

// (LiteralSymString): Compute symbol for a string literal
std::string LiteralSymString(std::string_view content);

// (LiteralSymBytes): Compute symbol for a bytes literal
std::string LiteralSymBytes(const std::vector<std::uint8_t>& content);

// (IsLiteralSymbol): Check if a symbol is a literal data symbol
// Used to determine linkage and optimization properties
bool IsLiteralSymbol(const std::string& symbol);

// ============================================================================
// §6.12.14 Literal IR Declaration Generation
// ============================================================================

// (EmitLiteralData-Decl): Create an IRDecl for literal data
// Generates a GlobalConst declaration with the literal's symbol and bytes
IRDecl EmitLiteralData(LiteralKind kind, const std::vector<std::uint8_t>& bytes);

// (EmitLiteral-String): Emit a string literal as GlobalConst
// The string is stored as a null-terminated UTF-8 byte array
IRDecl EmitStringLitDecl(std::string_view content);

// (EmitLiteral-Bytes): Emit a bytes literal as GlobalConst
// The bytes are stored directly without modification
IRDecl EmitBytesLitDecl(const std::vector<std::uint8_t>& content);

// (EmitLiteral-Char): Emit a char literal as GlobalConst
IRDecl EmitCharLitDecl(char32_t codepoint);

// (EmitLiteral-Int): Emit an integer literal as GlobalConst
IRDecl EmitIntLitDecl(const std::vector<std::uint8_t>& bytes);

// (EmitLiteral-Float): Emit a float literal as GlobalConst
IRDecl EmitFloatLitDecl(const std::vector<std::uint8_t>& bytes);

// ============================================================================
// §6.12.14 Literal Reference Collection
// ============================================================================

// (LiteralRefs): Collect all literal references from IR
// Returns a list of (kind, bytes) pairs for each literal used in the IR
std::vector<std::pair<LiteralKind, std::vector<std::uint8_t>>>
LiteralRefs(const IRPtr& ir);

// (LiteralRefs for decls): Collect literals from declarations
std::vector<std::pair<LiteralKind, std::vector<std::uint8_t>>>
LiteralRefs(const IRDecls& decls);

std::optional<LiteralKind> LiteralKindOfImmediate(const IRValue& value);
bool LiteralRef(const IRPtr& ir,
                LiteralKind kind,
                const std::vector<std::uint8_t>& bytes);
bool LiteralRef(const IRDecls& decls,
                LiteralKind kind,
                const std::vector<std::uint8_t>& bytes);

// ============================================================================
// §6.12.14 Literal Type Inference
// ============================================================================

// (StaticTypeForConst): Infer the type for a GlobalConst
// If type information is available in ctx, use it; otherwise infer [u8; n]
// only for literal-data symbols, where n is the byte count.
analysis::TypeRef StaticTypeForConst(const GlobalConst& global,
                                      const LowerCtx* ctx);

// ============================================================================
// §6.12.14 Literal Deduplication
// ============================================================================

// (UniqueLiterals): Deduplicate literal declarations
// Multiple identical literals should share the same global constant
std::vector<IRDecl> UniqueLiterals(
    const std::vector<std::pair<LiteralKind, std::vector<std::uint8_t>>>& lits);

// ============================================================================
// §6.12.14 String/Bytes View Construction
// ============================================================================

// (StringViewLayout): Get offsets for string@View fields
// string@View is { ptr: *imm u8, len: usize }
struct StringViewLayout {
  std::uint64_t ptr_offset = 0;
  std::uint64_t len_offset = 0;  // Platform-dependent (usually sizeof(ptr))
  std::uint64_t total_size = 0;  // sizeof(string@View)
};
StringViewLayout GetStringViewLayout(
    const analysis::layout::LayoutEnv& env);
StringViewLayout GetStringViewLayout(project::TargetProfile target_profile);
StringViewLayout GetStringViewLayout();

// (BytesViewLayout): Get offsets for bytes@View fields
// bytes@View is { ptr: *imm u8, len: usize }
// (Same layout as string@View)
StringViewLayout GetBytesViewLayout(
    const analysis::layout::LayoutEnv& env);
StringViewLayout GetBytesViewLayout(project::TargetProfile target_profile);
StringViewLayout GetBytesViewLayout();

// (EmitStringViewIR): Create IR to construct a string@View from a literal
// Returns IR that loads the global and constructs the view struct
IRPtr EmitStringViewIR(const std::string& literal_sym,
                       std::size_t length,
                       const IRValue& result,
                       LowerCtx& ctx);

// (EmitBytesViewIR): Create IR to construct a bytes@View from a literal
IRPtr EmitBytesViewIR(const std::string& literal_sym,
                      std::size_t length,
                      const IRValue& result,
                      LowerCtx& ctx);

// ============================================================================
// Spec Rule Anchors
// ============================================================================

// Emits SPEC_RULE anchors for §6.12.14 literal emission rules
void AnchorLiteralEmitRules();

}  // namespace ultraviolet::codegen
