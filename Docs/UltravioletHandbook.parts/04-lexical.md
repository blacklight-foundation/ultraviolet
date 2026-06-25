## 4. Source Text & Lexical Structure

This chapter is the canonical reference for the lowest two layers of an Ultraviolet compilation: how a file's raw bytes become a validated sequence of Unicode scalars (source loading, §4.1), and how that scalar sequence becomes a stream of tokens (lexical analysis, §4.2). It enumerates every keyword, every literal form, every operator and punctuator token, every comment form, and every source-loading and lexical diagnostic (§4.3). Code written from this chapter is what the parser (see "Parsing & AST Infrastructure") consumes; if a spelling does not appear here, it is not a token.

Two cross-cutting conventions used throughout this chapter:

- `T` denotes `S.scalars`, the validated Unicode scalar sequence of a source file `S`; `T[i]` is the scalar at index `i`, and `T[i..j)` is the half-open slice. Lexing operates on scalars, not on bytes; byte offsets in spans are recovered from `Utf8Offsets(T)`.
- `LF` is `U+000A`, `CR` is `U+000D`. After normalization the only line terminator in `T` is `LF`.

A note on what is *not* in this chapter. Items, declarations, and expression grammar belong to later chapters. The example code here is restricted to forms whose lexical shape is fully determined by §4 — module/item doc comments, `record` declarations, free `procedure` declarations, and module-scope `static_decl` bindings — so that every token shown is one this chapter actually defines. In particular, a module-scope constant is written `public let NAME: T = value` (a `static_decl`, §11.3); `const` is a *permission qualifier*, not a declaration keyword, and there is no `const NAME = …` item form.

---

### 4.1 Source Loading and Normalization

Source loading is the deterministic pipeline that turns a file's bytes `B` into a `SourceFile` record. A `SourceFile` carries everything later phases need:

```text
SourceFile = ⟨path, bytes, scalars, text, byte_len, line_starts, line_count⟩

S.text       = EncodeUTF8(S.scalars)
S.byte_len   = ByteLen(S.text)
S.line_count = |S.line_starts|
```

`scalars` is the canonical, normalized scalar sequence; `text` is its UTF-8 re-encoding; `line_starts` is the table of byte offsets at which each logical line begins. The pipeline is a state machine (`SourceLoadState`) with states

```text
Start → Sized → Decoded → BomStripped → Normalized → LineMapped → Validated
```

and an `Error(code)` sink. Each transition either advances the state or produces a diagnostic and halts (`LoadSource-Err`). The composed success rule **(LoadSource-Ok)** is the authoritative summary; it is reproduced in §4.1.6.

#### 4.1.1 Encoding: UTF-8 and Unicode Scalars

Source files MUST be valid UTF-8. The decoder is total only on well-formed input: `DecodeUTF8(B) = U` exactly when `EncodeUTF8(U) = B`, and `Utf8Valid(B) ⇔ ∃ U. DecodeUTF8(B) = U`. A `UnicodeScalar` is an integer in `[0, 0x10FFFF]` excluding the surrogate range `[0xD800, 0xDFFF]`; UTF-16 surrogate code points can never appear in a source file.

```text
Byte          = { n ∈ ℕ | 0 ≤ n ≤ 255 }
UnicodeScalar = { u ∈ ℕ | 0 ≤ u ≤ 0x10FFFF ∧ u ∉ [0xD800, 0xDFFF] }
String        = [UnicodeScalar]
```

If decoding fails, loading stops at the `Sized` state and emits `E-SRC-0101` (invalid UTF-8). Because the failure precedes any line mapping, this diagnostic has no span — rule **(NoSpan-Decode)** emits `Γ ⊢ Emit(E-SRC-0101, ⊥)`. A file whose bytes cannot be read at all yields `E-SRC-0102` (`ReadBytes-Err`).

#### 4.1.2 BOM Handling

A single UTF-8 byte-order mark (`U+FEFF`) at the very start of the file is permitted but discouraged: `StripBOM` removes it and the warning `W-SRC-0101` is emitted. Per rule **(Span-BOM-Warn)**, `W-SRC-0101` is emitted *even if* loading later fails for an unrelated reason.

A BOM that appears anywhere *after* the first position is an error. `StripBOM` reports the index `j` of the first embedded `U+FEFF`; if `j ≠ ⊥`, the pipeline transitions to `Error` and emits `E-SRC-0103` (embedded BOM, `Step-EmbeddedBOM-Err`) at that position.

```text
StripLeadBOM([])           = []
StripLeadBOM(U+FEFF :: U)  = U
StripLeadBOM(u :: U)       = u :: U   if u ≠ U+FEFF
```

Idiomatic Ultraviolet source carries no BOM at all.

#### 4.1.3 Line-Ending Normalization

All three line-ending conventions are accepted on input and collapsed to a single `LF`. `NormalizeLF` rewrites `CR LF` and a lone `CR` each to one `LF`; an existing `LF` is preserved and any other scalar passes through unchanged:

```text
NormalizeLF([])                = []
NormalizeLF([CR, LF] ++ U)     = [LF] ++ NormalizeLF(U)
NormalizeLF([CR]     ++ U)     = [LF] ++ NormalizeLF(U)   (when U = [] or U[0] ≠ LF)
NormalizeLF([LF]     ++ U)     = [LF] ++ NormalizeLF(U)
NormalizeLF([c]      ++ U)     = [c]  ++ NormalizeLF(U)   (when c ≠ CR ∧ c ≠ LF)
```

After this step every newline in `T` is exactly one `LF` scalar. This matters lexically: the `Newline` token (§4.2.13) is produced for `LF` only, and string/char literals forbid a literal `LF` in their body. Line and column numbers are computed against the normalized text, so they are stable regardless of the authoring platform's line endings.

The logical line map is derived from the normalized text:

```text
Utf8Offsets([])     = [0]
Utf8Offsets(c::cs)  = [0] ++ [o + Utf8Len(c) | o ∈ Utf8Offsets(cs)]

LineStarts(T) = [0] ++ [Utf8Offsets(T)[i] + 1 | 0 ≤ i < |T| ∧ T[i] = LF]
LineCount(T)  = |LineStarts(T)|
```

`Locate(S, o)` turns a byte offset into a `⟨file, offset, line, col⟩` record, with `line` and `col` both 1-based (it clamps `o` to `S.byte_len` first). This is the source of every diagnostic location in the language.

#### 4.1.4 Unicode Normalization

Outside identifiers and module paths, the source text is *not* normalized — `NormalizeOutsideIdentifiers(T) = T` is the identity. String and character literal contents are therefore preserved exactly as written, code point for code point.

Identifiers and module path segments are compared under Unicode Normalization Form C (NFC), Unicode 15.0.0:

```text
IdKey(s)       = NFC(s)
IdEq(s₁, s₂)   ⇔ IdKey(s₁) = IdKey(s₂)
PathKey(p)     = [NFC(c₁), …, NFC(cₙ)]
PathEq(p, q)   ⇔ PathKey(p) = PathKey(q)
```

Two identifiers that differ only by Unicode composition are the *same* identifier. This is why a literal's bytes survive untouched while an identifier's spelling is canonicalized for equality. `NFC` and `CaseFold` are total on scalar sequences; their inputs always come from `LoadSource`, which has already rejected invalid UTF-8.

#### 4.1.5 Prohibited Code Points

Control characters are forbidden in source except inside string/character literal spans — and only four whitespace controls are ever permitted at all in general source:

```text
Prohibited(c) ⇔ General_Category(c) = Cc ∧ c ∉ {U+0009, U+000A, U+000C, U+000D}
```

That is, only tab (`U+0009`), line feed (`U+000A`), form feed (`U+000C`), and carriage return (`U+000D`) are permitted control characters; every other `Cc` code point is prohibited unless its byte offset sits inside a string or character literal span (`LiteralSpan(T)`). A NUL byte or stray control character in code text produces `E-SRC-0104` (`Step-Prohibited-Err`), located at the first offending scalar via **(Span-Prohibited)**. (Note that after normalization `CR` no longer survives, so in finished source only tab, LF, and form feed appear as bare control characters.)

#### 4.1.6 The Loading Pipeline (Big-Step)

The composed success rule is the authoritative summary of §4.1:

```text
(LoadSource-Ok)
Γ ⊢ Decode(B) ⇓ U
Γ ⊢ StripBOM(U) ⇓ (U', b, ⊥)
Γ ⊢ NormalizeLF(NormalizeOutsideIdentifiers(U')) ⇓ T
L = LineStarts(T)
Γ ⊢ T : NoProhibited
S = ⟨path = f, bytes = B, scalars = T, text = EncodeUTF8(T),
     byte_len = ByteLen(T), line_starts = L, line_count = |L|⟩
─────────────────────────────────────────────────────────
Γ ⊢ LoadSource(f, B) ⇓ S
```

In order: decode UTF-8, strip a leading BOM and confirm no embedded BOM, normalize line endings (the outside-identifier normalization is the identity), build the line map, and confirm no prohibited code points. Any step that fails routes to `Error(c)`, and **(LoadSource-Err)** re-raises that code.

```ultraviolet
//! Pricing primitives for the storefront.
//!
//! This file loads cleanly: UTF-8, no BOM, LF newlines, no prohibited
//! control characters outside string and character literals.

/// The smallest representable price increment, in minor currency units.
public let PRICE_TICK: u32 = 1u32

/// A localized currency tag. Non-ASCII identifiers are permitted and are
/// compared under NFC, so this name is canonical regardless of how the
/// composing accents were entered.
public record Configuração {
    public symbol: char
    public minor_units: u32
}
```

---

### 4.2 Lexical Analysis

The lexer consumes a validated `SourceFile` and produces a token sequence `K` together with a list of doc comments `D`:

```text
Tokenize : SourceFile ⇀ (Token* × DocComment*)
LexerOutput(S) = ⟨K, D⟩    K ∈ Token*    D ∈ DocComment*
```

Every token is a record `⟨kind, lexeme, span⟩`. The token kinds are:

```text
TokenKind ∈ { Identifier, Keyword(k), IntLiteral, FloatLiteral,
              StringLiteral, CharLiteral, BoolLiteral, NullLiteral,
              Operator(o), Punctuator(p), Newline, Unknown }
```

A synthetic EOF token closes the stream: `TokenEOF(S) = ⟨EOF, ε, EOFSpan(S)⟩` with an empty lexeme at the end of input. Each token's lexeme is exactly the slice of `T` covered by its span (`LexemeBinding`).

The operator tokens `#`, `%`, `@`, and `$` may act as decorators: they prefix a
following token or token sequence while remaining ordinary `Operator` tokens.
Spellings such as `%read`, `@result`, `$(`, and `#dynamic` are source spellings,
not combined lexer tokens. For example, `%read` is `Operator("%")` followed by
`Identifier("read")`, and `@result` is `Operator("@")` followed by
`Identifier("result")`.

#### 4.2.1 Character Classes

The lexer's decisions are driven by a small set of scalar predicates:

```text
Whitespace(c) ⇔ c ∈ {U+0020, U+0009, U+000C}        (* space, tab, form feed *)
LineFeed(c)   ⇔ c = U+000A

IdentStart(c)    ⇔ c = '_' ∨ XID_Start(c)            (* UAX31 XID_Start, Unicode 15.0.0 *)
IdentContinue(c) ⇔ c = '_' ∨ XID_Continue(c)         (* UAX31 XID_Continue, Unicode 15.0.0 *)

DecDigit(c) ⇔ c ∈ {'0' … '9'}
HexDigit(c) ⇔ DecDigit(c) ∨ c ∈ {'a' … 'f', 'A' … 'F'}
OctDigit(c) ⇔ c ∈ {'0' … '7'}
BinDigit(c) ⇔ c ∈ {'0', '1'}
```

Note that the newline (`LF`) is *not* whitespace to the lexer — it produces a `Newline` token. Two further classes guard against malicious or ambiguous text:

```text
NonCharacter(c) ⇔ c ∈ [U+FDD0, U+FDEF] ∨ (c & 0xFFFF) ∈ {0xFFFE, 0xFFFF}
Sensitive(c)    ⇔ c ∈ {U+202A … U+202E, U+2066 … U+2069, U+200C, U+200D}
```

`Sensitive` covers the bidirectional-override and isolate controls plus the zero-width joiner/non-joiner; their handling is described in §4.2.14.

#### 4.2.2 Keywords and Reserved Lexemes

A keyword is any identifier-shaped lexeme that belongs to the reserved set. The reserved set is fixed and complete — exactly **49** lexemes:

```text
all      as       break    class    comptime continue copy     derive
dispatch else     enum     false    defer    frame    from     if
imm      import   internal let      loop     modal    move     mut
null     parallel private  procedure public  quote    race     record
region   return   shared   spawn    sync     transition transmute true
type     unique   unsafe   var      widen    using    yield    const
override
```

That is, `Reserved` is exactly that 49-element set; `Keyword(s) ⇔ s ∈ Reserved`, and `FutureReserved = ∅`. Three of these lexemes are reclassified during identifier lexing rather than emitted as `Keyword` (see `ClassifyIdent`, §4.2.3):

- `true` and `false` lex as `BoolLiteral`.
- `null` lexes as `NullLiteral`.

The remaining 46 reserved lexemes emit `Keyword(s)`.

`in`, `key`, `wait`, and `new` are contextual keywords. They lex as
`Identifier` tokens and are recognized only in the syntactic positions that
own those forms: dispatch/key syntax, `wait` expressions, and current-region
allocation. This is why qualified names such as `CancelToken::new` remain
ordinary identifier paths.

Beyond the hard keywords, the spec reserves two further categories (both activate in `Phase3`):

- **Reserved namespaces and prefixes.** The module prefix `ultraviolet::` and the identifier prefix `gen_` are reserved (`ReservedNamespacePrefix`, `ReservedIdentPrefix`). Do not introduce a top-level `ultraviolet` namespace, and do not name your own bindings `gen_…`.
- **Universe-protected bindings.** A fixed set of built-in names (`UniverseProtected`) is reserved at the binding level. This includes every primitive type (`i8`…`i128`, `u8`…`u128`, `f16`/`f32`/`f64`, `bool`, `char`, `usize`, `isize`), `string`, `bytes`, `Self`, the built-in foundational class names, and the standard capability/runtime types (for example `Region`, `RegionOptions`, `CancelToken`, `Context`, `System`, `IO`, `HeapAllocator`, `Network`, `Reactor`, `Time`, `Duration`, `Future`, `Stream`, and others). In particular `Drop`, `Bitcopy`, `Clone`, `FfiSafe`, and `GpuSafe` are reserved foundational class names and MUST NOT be declared as classes or used as user-defined type/value bindings.

These protected names are *not* keywords — they lex as `Identifier`; the protection is a binding-level rule enforced after lexing. They are listed here because they constrain what identifiers you may legally write. See the type and primitives chapters for the full semantics of the universe-protected types.

```ultraviolet
// `record`, `public`, and `return` are keywords; `Vector3`, `length_squared`,
// and `value` are ordinary identifiers; `f32` is a universe-protected
// primitive type name (an identifier, not a keyword).
public record Vector3 {
    public x: f32
    public y: f32
    public z: f32
}

public procedure lengthSquared(value: Vector3) -> f32 {
    return value.x * value.x + value.y * value.y + value.z * value.z
}
```

#### 4.2.3 Identifiers

```ebnf
identifier     ::= ident_start ident_continue*
ident_start    ::= (* Unicode XID_Start *) | "_"
ident_continue ::= (* Unicode XID_Continue *) | "_"
```

An identifier starts with `_` or an `XID_Start` scalar and continues with `_` or `XID_Continue` scalars. The scan is maximal: `IdentScanEnd` extends to the first scalar that is not `IdentContinue`. After scanning, `ClassifyIdent` decides the kind:

```text
ClassifyIdent(s) =
  BoolLiteral  if s ∈ {"true", "false"}
  NullLiteral  if s = "null"
  Keyword(s)   if Keyword(s)
  Identifier   otherwise
```

If any scalar in the identifier's span is a Unicode non-character (`NonCharacter`), the lexer emits `E-SRC-0307` (invalid Unicode in identifier, `Lex-Ident-InvalidUnicode`) at the first such position. Identifiers are valid under any script that UAX31 admits, but two additional security rules (§4.2.14) reject confusable and mixed-script identifiers.

The style guide governs *how* you should name identifiers even though the lexer permits much more: types in `PascalCase`, procedures/methods/transitions in `camelCase`, locals/parameters/instance fields in `snake_case`, private instance fields in `_snake_case`, constants and statics in `SCREAMING_SNAKE` (private statics `_SCREAMING_SNAKE`), boolean variables/fields with an `is`/`has`/`can`/`should` prefix (`is_ready`), boolean procedures/methods with the same predicate prefix in `camelCase` (`isReady`), and generic type parameters as `PascalCase` with a `T` prefix (`TValue`). Do not encode type information in names, and alias only with `using … as …` when genuinely needed.

```ultraviolet
public let MAX_SUBTICKS: u32 = 240u32     // SCREAMING_SNAKE module-scope static

public procedure tick(frame_index: u32) -> bool {
    let is_ready: bool = frame_index < MAX_SUBTICKS   // predicate snake_case
    return is_ready
}
```

#### 4.2.4 Integer Literals

```ebnf
integer_literal ::= (decimal_integer | hex_integer | octal_integer | binary_integer) int_suffix?
decimal_integer ::= dec_digit ("_"* dec_digit)*
hex_integer     ::= "0x" hex_digit ("_"* hex_digit)*
octal_integer   ::= "0o" oct_digit ("_"* oct_digit)*
binary_integer  ::= "0b" bin_digit ("_"* bin_digit)*
int_suffix      ::= "i8" | "i16" | "i32" | "i64" | "i128"
                  | "u8" | "u16" | "u32" | "u64" | "u128"
                  | "isize" | "usize"
dec_digit ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
hex_digit ::= dec_digit | "a" | "b" | "c" | "d" | "e" | "f"
                        | "A" | "B" | "C" | "D" | "E" | "F"
oct_digit ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7"
bin_digit ::= "0" | "1"
```

There are four bases:

- **Decimal:** bare digits, e.g. `42`.
- **Hexadecimal:** prefix `0x`, e.g. `0xFF`. Both upper- and lower-case hex digits are accepted.
- **Octal:** prefix `0o`, e.g. `0o755`.
- **Binary:** prefix `0b`, e.g. `0b1010`.

**Digit separators.** An underscore `_` may appear *between* digits to group them. The grammar `("_"* dec_digit)*` allows runs of underscores between digits, but the underscore-placement rule (`NumericUnderscoreOk`) tightens this: a literal MUST NOT start or end with `_`, MUST NOT place `_` immediately after a base prefix, MUST NOT place `_` adjacent to an exponent marker, and MUST NOT place `_` immediately before a suffix:

```text
NumericUnderscoreOk(s) ⇔ ¬StartsWithUnderscore(s) ∧ ¬EndsWithUnderscore(s)
                        ∧ ¬AfterBasePrefixUnderscore(s)
                        ∧ ¬AdjacentExponentUnderscore(s)
                        ∧ ¬BeforeSuffixUnderscore(s)
```

**Type suffixes.** An integer literal may carry an explicit width/signedness suffix from `int_suffix`: `i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 isize usize`. The suffix is part of the literal token — it is munched with the digits (`SuffixMatch` over `NumSuffix`), not lexed as a following identifier.

**Lexing.** The numeric scanner takes a maximal digit run for the base (`HexRun`/`OctRun`/`BinRun`/`DecRun`), then matches an optional suffix. A numeric lexeme whose core contains no `.` is an `IntLiteral` (`NumericKind = IntLiteral`). A malformed numeric lexeme — for instance, an out-of-place underscore, or a `0x` with no hex digits — produces `E-SRC-0304` (`Lex-Numeric-Err`) via `NumericLexemeBad`.

**Leading zeros.** A decimal literal whose significant-digit string (underscores removed) is longer than one and begins with `0` (e.g. `007`) triggers the warning `W-SRC-0301`. This applies to *decimal* literals only; `0x`, `0o`, `0b`, and a bare `0` are fine.

```ultraviolet
public let small: u8     = 255u8
public let mask: u32     = 0xFF00_00FFu32      // grouped hex, no underscore after 0x
public let perms: u16    = 0o755u16
public let flags: u8     = 0b1010_0101u8
public let big: i128     = 170_141_183_460i128 // decimal grouping
public let count: usize  = 1_000usize
```

#### 4.2.5 Floating-Point Literals

```ebnf
float_literal ::= decimal_integer "." decimal_integer? exponent? float_suffix?
exponent      ::= ("e" | "E") ("+" | "-")? decimal_integer
float_suffix  ::= "f" | "f16" | "f32" | "f64"
```

A float literal is a decimal integer followed by a `.`, with an optional fractional part, an optional decimal exponent, and an optional float suffix. The presence of a `.` in the numeric core (a "float core") is what distinguishes a `FloatLiteral` from an `IntLiteral`:

```text
HasFloatCore(T, i, j) ⇔ HasDot(T, i, j)
NumericKind(T, i) = FloatLiteral  if HasFloatCore(T, i, NumericCoreEnd(T, i))
                  = IntLiteral     otherwise
```

Float literals exist only in **decimal**: there are no hexadecimal, octal, or binary floats. The exponent marker is `e` or `E`, optionally followed by `+` or `-` and then decimal digits. Digit separators follow the same `NumericUnderscoreOk` rule as integers, including the prohibition on an underscore directly adjacent to the exponent marker.

**Suffix and defaulting.** Float literals MAY omit a suffix when they contain a decimal point. The suffixes are:

- `f` — a float literal whose width is inferred from context.
- `f16`, `f32`, `f64` — an explicit width.

If no expected type is present, an unsuffixed decimal float literal defaults to `f32`. If an expected or declared float type is present, that expected type governs checking of unsuffixed and `f`-suffixed literals. Using an explicit width suffix that conflicts with the expected type is an error.

**Two lexer disambiguations are essential to get right:**

1. **Range operators win over the dot.** A decimal run immediately followed by `..` or `..=` does *not* form a float core (`DecCoreEnd` stops at the `DecRun`, leaving the `.` for operator tokenization). So `3..7` lexes as `IntLiteral(3)`, `Operator("..")`, `IntLiteral(7)` — the dot belongs to the range operator, not to a float `3.`.
2. **Tuple field access wins over the dot.** When the most recently emitted token has lexeme `"."`, a following decimal that could lex as either an integer (ending at `j_i`) or a float (ending at `j_f`, with `j_i < j_f`) is lexed as the *integer*, leaving the `.` available. This makes `pair.0` and `nested.0.1` (tuple field projection) lex correctly.

```ultraviolet
public let ratio: f32    = 0.5            // unsuffixed → f32 by default
public let exact: f64    = 1.0f64         // explicit width
public let inferred: f16 = 2.5f           // `f` width inferred from declared type
public let scaled: f64   = 6.022e23f64    // exponent form
public let stepped: f32  = 1_000.000_5    // digit separators on both sides
```

#### 4.2.6 Boolean and Null Literals

```ebnf
bool_literal ::= "true" | "false"
null_literal ::= "null"
```

`true` and `false` are the only boolean literals and lex as `BoolLiteral`. `null` lexes as `NullLiteral`. All three are reserved lexemes, so they can never be used as identifiers. `null` denotes the null safe-pointer state and is not a general "nil" value; see the pointer and memory chapters for its typing.

```ultraviolet
public let can_present: bool = false
public let has_focus: bool   = true
```

#### 4.2.7 Character Literals

```ebnf
char_literal ::= "'" (char_content | escape_sequence) "'"
char_content ::= (* Unicode scalar except ', \, or U+000A *)
```

A character literal is a single Unicode scalar — either one literal scalar or one escape sequence — between apostrophes. Its representation is `u32` (`CharRepr = u32`, `SizeOf(char) = 4`, `AlignOf(char) = 4`), holding a scalar in `[0, 0x10FFFF]` minus the surrogate range; `char` is a full Unicode scalar value, not a byte. The body of a char literal MUST contain exactly one scalar after escape resolution:

```text
CharLiteralInvalid(T, i) ⇔ CharScalarCount(T, i) ≠ 1
```

A char literal with zero or more than one scalar (e.g. `''` or `'ab'`) emits `E-SRC-0303` (`Lex-Char-Invalid`). A bad escape inside a char literal emits `E-SRC-0302` (`Lex-Char-BadEscape`) at the offending backslash. An unterminated char literal — one whose closing `'` is preceded by a newline (`LF`) or end of file — emits `E-SRC-0303` (`Lex-Char-Unterminated`); the lexer recovers by skipping to the terminator (`CharTerminator`).

The forbidden bare characters inside a char literal body are the apostrophe (`'`), the backslash (`\`), and `LF`; each of those must be written as an escape. Any other scalar may be written directly.

```ultraviolet
public let letter: char    = 'A'
public let newline: char   = '\n'
public let quote: char     = '\''
public let backslash: char = '\\'
public let tab: char       = '\t'
public let hex_unit: char  = '\x41'         // 'A'
public let snowman: char   = '\u{2603}'     // ☃ by code point
public let direct: char    = '☃'            // the scalar written directly
```

#### 4.2.8 String Literals

```ebnf
string_literal ::= '"' (string_char | escape_sequence)* '"'
string_char    ::= (* Unicode scalar except ", \, or U+000A *)
```

A string literal is a sequence of string characters and escape sequences between double quotes. A bare double quote, backslash, or `LF` cannot appear in the body — the double quote and backslash must be escaped, and a literal newline is not permitted at all. There is **no raw string form, no multiline string form, and no string-continuation form** in the language: a string literal is confined to a single logical line. Long text is built from ordinary single-line string literals.

The closing delimiter is the next *unescaped* double quote. "Unescaped" is defined by counting preceding backslashes: a quote is a terminator iff an even number of backslashes precede it.

```text
BackslashCount(T, p) = max{ k | 0 ≤ k ≤ p ∧ ∀ r ∈ [p-k, p). T[r] = "\" }
UnescapedQuote(T, p) ⇔ T[p] = '"' ∧ BackslashCount(T, p) mod 2 = 0
StringTerminator(T, i) = min{ q | q > i ∧ (UnescapedQuote(T, q) ∨ T[q] = LF ∨ q = |T|) }
```

If a `LF` or end of file is reached before the closing quote, the string is unterminated: `E-SRC-0301` (`Lex-String-Unterminated`) is emitted and the lexer recovers by advancing to the terminator. A malformed escape inside the string emits `E-SRC-0302` (`Lex-String-BadEscape`) at the offending backslash. Critically, a *terminated* quoted span always forms a `StringLiteral` token even when its interior is ill-formed — a bad escape emits its diagnostic but does *not* suppress the token. Only *unterminated* quoted spans are excluded from token formation.

```ultraviolet
public let greeting: string   = "Hello, world"
public let path: string       = "C:\\Dev\\Projects"      // escaped backslashes
public let quoted: string     = "She said \"hi\""        // escaped quotes
public let with_tab: string   = "name:\tvalue"
public let unicode: string    = "snowman: \u{2603}"
public let hex_escape: string = "byte: \x7F"
```

**A note on `bytes`.** The language has a `bytes` *type* (see the primitives/string chapter and `bytes_type ::= "bytes" ("@" bytes_state)?` in Appendix B.2), but there is **no bytes literal token** in the lexical grammar — no `b"…"` form exists. `bytes` values are produced through the type's API, not through a dedicated literal spelling. Do not write `b"…"`; it is not valid Ultraviolet.

#### 4.2.9 Escape Sequences

Both string and character literals share one escape grammar:

```ebnf
escape_sequence ::= "\n" | "\r" | "\t" | "\\" | "\"" | "\'" | "\0"
                  | "\x" hex_digit hex_digit
                  | "\u{" hex_digit+ "}"
```

The complete set of valid escapes and their values (`EscapeValue`):

```text
\\  → 0x5C   (backslash)        \n  → 0x0A   (line feed)
\"  → 0x22   (double quote)     \r  → 0x0D   (carriage return)
\'  → 0x27   (apostrophe)       \t  → 0x09   (tab)
\0  → 0x00   (NUL)
\x HH       → the byte HexValue(HH)                            (exactly two hex digits)
\u{ H… }    → EncodeUTF8(HexValue(H…)) of the Unicode scalar   (1 to 6 hex digits)
```

The simple escapes are exactly `SimpleEscape = {\\, \", \', \n, \r, \t, \0}`. Validity rules for the two parameterized forms:

- `\x` requires *exactly two* hex digits.
- `\u{…}` requires between 1 and 6 hex digits and the resulting value MUST be a valid `UnicodeScalar` (≤ `0x10FFFF`, not a surrogate).

```text
EscapeOk("\x" h₁ h₂)      ⇔ HexDigit(h₁) ∧ HexDigit(h₂)
EscapeOk("\u{" h₁…hₙ "}") ⇔ 1 ≤ n ≤ 6 ∧ UnicodeScalar(HexValue(h₁…hₙ))
```

Any backslash that does not begin one of these forms is a bad escape and emits `E-SRC-0302` (`Lex-Char-BadEscape` or `Lex-String-BadEscape`). Note there is no `\f`, no `\v`, no `\a`, no octal escape, and no brace-less `\uXXXX` form — only the escapes above are valid.

#### 4.2.10 Operators and Punctuators

Operators and punctuators are two disjoint token sets (`OperatorSet ∩ PunctuatorSet = ∅`). The complete operator set, exactly as spelled:

```text
+    -    *    /    %    **
==   !=   <    <=   >    >=
&&   ||   !
&    |    ^    <<   >>
=    +=   -=   *=   /=   %=   &=   |:   ^=   <<=  >>=
:=   <:   ..   ..=  |=   =>
->   ::   ~    ~>   ~!   ~%
?    #    @    $
```

The complete punctuator set:

```text
(    )    [    ]    {    }    ,    :    ;    .
```

A few tokens carry special lexical roles worth calling out:

- `#` is the **attribute delimiter** operator. It prefixes exactly one attribute specification; repeated adjacent attributes form an attribute list. (Attribute *syntax* is covered in the attributes chapter. Note that inside a `record` body the same `#` token also serves as the `key_boundary` marker.)
- `::` is the path separator; `.` (a *punctuator*) is field/method access and tuple projection; `~>` is the dispatch-method-call access used in `postfix_suffix`; `->` is the function-return arrow.
- `|:` introduces a refinement clause; `<:` introduces a class bound; `:=` is the alternative binding operator (`binding_op ::= "=" | ":="`); `=` is the primary binding/assignment operator.
- `..` and `..=` are the exclusive and inclusive range operators (see the float-literal disambiguation in §4.2.5).
- The unit literal `()` is two punctuator tokens — an open and a close paren — not a single literal token (`unit_literal ::= "(" ")"`).

Operator and punctuator tokens are matched by exact lexeme equality against these sets; the maximal-munch rule (§4.2.12) selects the longest match (so `>>=` is preferred over `>>` then `=`, `..=` over `..` then `=`, and `==` over two `=`).

```ultraviolet
public procedure combine(base: i32, bonus: i32, value: i32) -> i32 {
    let total: i32   = base + bonus * 2i32       // arithmetic operators
    let scaled: i32  = value << 3i32             // shift
    let in_range: bool = base <= value && value < total  // comparison + logical
    if in_range
        return scaled
    return total
}
```

#### 4.2.11 Comments and Doc Comments

There are four comment forms. Two are non-documentation comments discarded by the lexer; two are doc comments that the lexer collects into `D`. None of the four produces a token in `K` (`LexNoComments`).

**Line comments.** `//` begins a line comment that runs to (but not including) the next `LF`:

```text
ScanLineComment(T, i) ⇓ j   where j = min{ p | i ≤ p ∧ (p = |T| ∨ T[p] = LF) }
```

A `//` line comment is taken only when `T[i..i+3] ∉ {"///", "//!"}` (otherwise it is a doc comment).

**Block comments (nested).** `/* … */` is a block comment, and block comments **nest**: each `/*` increments a depth counter and each `*/` decrements it; the comment ends when depth returns to zero. A block comment whose nesting never closes before end of input emits `E-SRC-0306` (`Block-Comment-Unterminated`), located at the opening `/*`.

```text
(Block-Start)  T[i..i+2] = "/*"            →  depth + 1
(Block-End)    T[i..i+2] = "*/", depth > 1 →  depth − 1
(Block-Done)   T[i..i+2] = "*/", depth = 1 →  done
```

**Doc comments.** Two markers, both line-oriented, are classified as documentation rather than discarded:

```text
DocMarker(T, i) = LineDoc    if T[i..i+3] = "///"
                = ModuleDoc  if T[i..i+3] = "//!"
                = ⊥          otherwise
```

- `///` is an **item/line doc comment** (`LineDoc`) that documents the following item.
- `//!` is a **module doc comment** (`ModuleDoc`) that documents the enclosing module/file.

A doc comment's body is the text after the three-character marker, with a single leading space stripped (`StripLeadingSpace`):

```text
DocBody(T, i, j) = StripLeadingSpace(T[i+3..j))
```

Each doc comment becomes a `DocComment = ⟨kind, text, span⟩` record appended to `D`; it produces no token in `K`. Because the classification is on the exact three-character prefix, a run of four or more slashes is still a `LineDoc`: for `////`, `T[i..i+3] = "///"` so it is classified `LineDoc`, and its body begins with the extra `/`. To write an ordinary, non-doc line comment, use exactly `//` followed by a non-`/`, non-`!` character (or `//` at end of line).

```ultraviolet
//! Storefront pricing module.
//!
//! Centralizes price math so rounding rules live in exactly one place.

/// Computes the gross price for `quantity` units at `unit_price`.
///
/// Precondition: `quantity` fits the configured maximum line size.
/// Postcondition: the result never overflows `u64`.
public procedure grossPrice(unit_price: u32, quantity: u32) -> u64 {
    // Widen before multiplying so the product cannot overflow u32.
    let price: u64 = unit_price as u64
    let count: u64 = quantity as u64
    return price * count   /* nested /* block */ comments are allowed here */
}
```

#### 4.2.12 Maximal Munch and Token Selection

At each non-whitespace, non-comment position the lexer forms the candidate set for the relevant class and picks the longest match:

```text
Candidates(T, i) =
  StringTok(T, i) ∪ CharTok(T, i)   if IsQuote(T[i])
  FloatTok(T, i) ∪ IntTok(T, i)     if DecDigit(T[i])
  IdentToken(T, i)                  if IdentStart(T[i])
  OpTok(T, i) ∪ PuncTok(T, i)       if OpTok(T, i) ≠ ∅ ∨ PuncTok(T, i) ≠ ∅
  ∅                                 otherwise
```

Among the candidates, `PickLongest` maximizes first the end index `j` (longest lexeme), then breaks length ties by kind priority:

```text
KindPriority: literals = 3, Identifier/Keyword = 2, Operator = 1, Punctuator = 0
PickLongest(C) = argmax_{(k, j) ∈ C} ⟨j, KindPriority(k)⟩
```

If no candidate matches at a non-whitespace, non-comment, non-sensitive position, tokenization fails with `E-SRC-0309` (`Max-Munch-Err`). (`GenericCloseException = false`: there is no special-case splitting of a `>>` token at a generic close — `>>` always munches as a single shift operator at the lexical level.)

#### 4.2.13 Newlines and Statement Termination

**Newline tokens.** A `LF` that is not inside a literal or comment becomes a `Newline` token:

```text
NewlineTokenAt(S, T, i) ⇔ T[i] = LF ∧ ¬ InsideLiteralOrComment(i)
```

Newlines are significant: they are the default statement terminator (`IsTerminator(t) ⇔ t = Punctuator(";") ∨ t.kind = newline`). The parser layer decides which newlines actually terminate a statement and which are absorbed as line continuations: `Continue(K, i)` is true when bracket depth is positive (inside an unclosed `(` or `[`), after a trailing comma, after certain trailing operators, before a `.`/`::`/`~>` continuation, after an attribute, or in the `} else` continuation (`ElseCont`). `Filter(K)` removes every continued newline; a required terminator with no terminator token at its boundary yields the `Missing-Terminator-Err` diagnostic. The detailed predicate belongs to the parsing chapter; what the lexer guarantees is simply that exactly one `Newline` token is produced per non-literal, non-comment `LF`.

Commas are *separators within* a statement and are never statement terminators. Per the style guide, prefer newline statement termination; use `;` only when several small statements genuinely belong on one line or surrounding syntax requires it.

#### 4.2.14 Lexical Security: Bidi, Confusables, and Mixed Scripts

Three checks defend against visually deceptive source:

1. **Lexically sensitive Unicode** (bidi overrides/isolates and zero-width joiners, the `Sensitive` set). Outside literals and comments, a sensitive character is an **error** — `E-SRC-0308` (`LexSecure-Err`) — *unless* its byte falls inside an `unsafe { … }` span (`UnsafeAtByte`), where it is downgraded to the warning `W-SRC-0308`. Sensitive characters inside string/character literals and inside comments are never flagged (they are excluded from `Sens` by `InsideLiteralOrComment`). This is the only place where being inside `unsafe` changes lexical legality.
2. **Confusable identifiers.** If two identifier lexemes share a confusable skeleton but are not NFC-equal (`ConfusablePair`), the program is rejected with `E-SRC-0310` (`Confusable-Err`).
3. **Mixed-script identifiers.** An identifier mixing more than one non-Common, non-Inherited script (`MixedScript`) is rejected with `E-SRC-0311` (`MixedScript-Err`).

These checks make "looks the same, is different" attacks non-compiling. They are a reason to keep identifiers in a single script and to avoid invisible formatting characters entirely in code.

---

### 4.3 Source Loading and Lexical Diagnostics

This section is the canonical registry of every source-loading and lexical diagnostic. All are detected at compile time. (There is no `E-SRC-0105` or `E-SRC-0305`; the codes are not contiguous.)

| Code         | Severity | Condition                                                                 |
| ------------ | -------- | ------------------------------------------------------------------------- |
| `E-SRC-0101` | Error    | Invalid UTF-8 byte sequence (`Step-Decode-Err`)                           |
| `E-SRC-0102` | Error    | Failed to read source file (`ReadBytes-Err`)                              |
| `E-SRC-0103` | Error    | Embedded BOM found after the first position (`Step-EmbeddedBOM-Err`)      |
| `E-SRC-0104` | Error    | Forbidden control character or null byte (`Step-Prohibited-Err`)          |
| `E-SRC-0301` | Error    | Unterminated string literal (`Lex-String-Unterminated`)                   |
| `E-SRC-0302` | Error    | Invalid escape sequence (`Lex-Char-BadEscape`, `Lex-String-BadEscape`)    |
| `E-SRC-0303` | Error    | Invalid character literal (`Lex-Char-Invalid`, `Lex-Char-Unterminated`)   |
| `E-SRC-0304` | Error    | Malformed numeric literal (`Lex-Numeric-Err`)                             |
| `E-SRC-0306` | Error    | Unterminated block comment (`Block-Comment-Unterminated`)                 |
| `E-SRC-0307` | Error    | Invalid Unicode in identifier (`Lex-Ident-InvalidUnicode`)                |
| `E-SRC-0308` | Error    | Lexically sensitive Unicode outside `unsafe` block (`LexSecure-Err`)      |
| `E-SRC-0309` | Error    | Tokenization could not classify a character sequence (`Max-Munch-Err`)    |
| `E-SRC-0310` | Error    | Confusable identifier pair (`Confusable-Err`)                             |
| `E-SRC-0311` | Error    | Mixed-script identifier (`MixedScript-Err`)                               |
| `W-SRC-0101` | Warning  | UTF-8 BOM present at the start of the file                                |
| `W-SRC-0301` | Warning  | Leading zeros in a decimal literal                                        |
| `W-SRC-0308` | Warning  | Lexically sensitive Unicode within an `unsafe` block                      |

---

### Idioms & Best Practices

- **Save files as UTF-8 with no BOM and LF line endings.** A BOM produces `W-SRC-0101` (or `E-SRC-0103` if embedded), and although CR/CRLF are normalized, LF-only files avoid surprises. This keeps loading warning-free.
- **Write module-scope constants as `public let NAME: T = value`.** `const` is a permission qualifier, not a declaration keyword — there is no `const` item form. A module-scope constant is a `static_decl` (`visibility? ("let" | "var") binding_decl`); `public var` is rejected, so public statics use `let`.
- **Keep numeric literals readable with digit separators.** Group long values (`170_141_183_460i128`, `0xFF00_00FFu32`, `0b1010_0101u8`) but never place `_` after a base prefix, adjacent to an exponent marker, or directly before a suffix — those are `E-SRC-0304`.
- **Make literal widths explicit at boundaries.** Suffix integer literals (`255u8`, `1_000usize`) and float literals (`1.0f64`) where the target type matters; rely on the `f32` default for unsuffixed floats only when context makes the intent obvious. Prefer `f` (context-inferred) over an explicit width that could conflict with a declared type and become an error.
- **Prefer newline statement termination.** Let the line break end statements; reserve `;` for the rare justified case of multiple small statements on one line. Commas separate elements; they never terminate statements.
- **Document public surfaces.** Every public module gets `//!` documentation; every public type, procedure, method, transition, and exported constant gets `///` documentation covering purpose, important pre/postconditions, ownership/capability expectations, and notable failure modes. Use `//` only for non-obvious *why* commentary, not to narrate clear code.
- **Name per the naming matrix.** `PascalCase` types, `camelCase` procedures/methods/transitions, `snake_case` locals/parameters/instance fields, `_snake_case` private fields, `SCREAMING_SNAKE` constants/statics (`_SCREAMING_SNAKE` private statics), predicate prefixes for booleans, `T`-prefixed `PascalCase` generic parameters.
- **Keep identifiers single-script and visible.** Avoid zero-width and bidi-control characters entirely; keep each identifier in one script to stay clear of the confusable (`E-SRC-0310`) and mixed-script (`E-SRC-0311`) checks.

### Pitfalls & Diagnostics

- **`const` is not a declaration keyword.** `const NAME = …` is not an item form; it will not parse as a constant. Use `public let NAME: T = value`. `const` appears only as a permission qualifier in type position.
- **`\x` needs exactly two hex digits; `\u{…}` needs braces and 1–6 digits.** `'\x4'` and `'é'` (no braces) are bad escapes (`E-SRC-0302`). There is no `\f`, `\v`, `\a`, octal, or brace-less unicode escape.
- **A char literal holds exactly one scalar.** `''` and `'ab'` are `E-SRC-0303`. Multi-scalar text must be a string.
- **No raw, multiline, or continuation strings exist.** A `"…"` may not contain a literal newline; an unclosed quote before the line ends is `E-SRC-0301`. Build long text from single-line literals.
- **No `bytes` literal exists.** `b"…"` is not a token. Use the `bytes` type's API instead.
- **`..`/`..=` and tuple `.0` defeat the float dot.** `3..7` is two integers around a range operator, and `pair.0` is tuple projection — neither produces a float `3.`. Write `3.0..7.0` if you actually want float endpoints.
- **`//!` and `///` are collected, not discarded.** They are doc comments, not ordinary comments. To write a plain comment, use `//` followed by something other than `/` or `!`. A bare `//` at end of line is plain. Four-or-more slashes (`////`) still classify as `///` doc comments whose body starts with the extra slash.
- **Block comments nest and must balance.** An unbalanced `/*` swallows the rest of the file and yields `E-SRC-0306`; remember that an inner `/*` you did not intend to open still increments depth.
- **Reserved and protected names are not free identifiers.** The 49 keywords (`type`, `record`, `move`, …) cannot be identifiers, and universe-protected names (`i32`, `Self`, `Drop`, `Clone`, `Bitcopy`, `FfiSafe`, `string`, `bytes`, the standard capability/runtime types, …) cannot be redeclared as your own bindings, nor may you create an `ultraviolet::` namespace or `gen_…` identifier.
- **Bidi and zero-width controls are errors in code.** Outside literals/comments they are `E-SRC-0308` unless inside `unsafe { … }` (then `W-SRC-0308`). Do not rely on `unsafe` to launder deceptive text; keep it out of source.
- **Leading zeros warn.** `007` triggers `W-SRC-0301`; write `7` (decimal) or use an explicit base prefix when a zero prefix is meaningful.
