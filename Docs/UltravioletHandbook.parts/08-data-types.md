## 8. Primitive & Aggregate Data Types

This chapter is the exhaustive reference for Ultraviolet's concrete data types as defined in §12 of the specification: the primitive scalar types (§12.1), tuples (§12.2), arrays (§12.3), slices (§12.4), ranges (§12.5), records (§12.6), enums (§12.7), union types (§12.8), and type aliases (§12.9). For every construct it gives the verbatim grammar, the precise static and dynamic semantics, idiomatic worked examples, and the diagnostics the compiler emits. Modal types, `string`, `bytes`, pointer types, and refinement types are *not* the subject of this chapter; see the chapter "Modal & Special Types" for those. (`string` and `bytes` are grouped under `primitive_type` in the Appendix B type grammar but are sized text/byte-buffer types, not scalars, and are covered there.)

A note on the metavariables used throughout: `T`, `T_0`, `T_i` denote types; `e`, `e_i` denote expressions; `TypePrim`, `TypeTuple`, `TypeArray`, `TypeSlice`, `TypeUnion`, `TypePath`, and the `TypeRange*` constructors are the internal AST type forms. The judgement `Γ ⊢ T wf` reads "type `T` is well-formed in context `Γ`"; `Γ ⊢ e : T` reads "expression `e` has type `T`". You do not write these forms in source — they are the rules a conforming compiler enforces against the source you *do* write.

### 8.1 Primitive Types

Primitive types are the indivisible scalar building blocks of the language. There is no implementation-defined surface scalar beyond the names below.

#### 8.1.1 Exact syntax

The §12.1.1 grammar:

```ebnf
primitive_type  ::= signed_int_type
                  | unsigned_int_type
                  | float_type
                  | "bool"
                  | "char"
                  | "(" ")"
                  | "!"
signed_int_type   ::= "i8" | "i16" | "i32" | "i64" | "i128" | "isize"
unsigned_int_type ::= "u8" | "u16" | "u32" | "u64" | "u128" | "usize"
float_type        ::= "f16" | "f32" | "f64"
```

The Appendix B type grammar (B.2) groups the same set, additionally naming the sub-categories and folding `string`/`bytes` into `primitive_type`:

```ebnf
primitive_type ::= integer_type | float_type | bool_type | char_type | unit_type | never_type | string_type | bytes_type
integer_type   ::= signed_int | unsigned_int | pointer_int
signed_int     ::= "i8" | "i16" | "i32" | "i64" | "i128"
unsigned_int   ::= "u8" | "u16" | "u32" | "u64" | "u128"
pointer_int    ::= "isize" | "usize"
float_type     ::= "f16" | "f32" | "f64"
bool_type      ::= "bool"
char_type      ::= "char"
unit_type      ::= "(" ")"
never_type     ::= "!"
```

The complete set of scalar primitive type names recognised by the parser (`PrimLexemeSet`) is exactly:

`i8`, `i16`, `i32`, `i64`, `i128`, `isize`, `u8`, `u16`, `u32`, `u64`, `u128`, `usize`, `f16`, `f32`, `f64`, `bool`, `char`

plus the two punctuation/operator-spelled types: the unit type `()` and the never type `!`. The full `PrimitiveTypeName` set used by the AST is `PrimLexemeSet ∪ {()​, !}`.

There is **no** `unit` keyword. The unit type is spelled `()` everywhere — as a type and as a value. Likewise there is no `void`, `never`, `int`, `float`, `byte`, or `usz` spelling; only the exact names above are accepted.

#### 8.1.2 Integer types — exact names, widths, and signedness

Every integer type, its exact spelling, signedness, bit width, and value range (`IntWidth`, `RangeOf`):

| Type    | Signedness | Width (bits)            | Range (`RangeOf`)                          |
| ------- | ---------- | ----------------------- | ------------------------------------------ |
| `i8`    | signed     | 8                       | `[-2^7, 2^7 - 1]`                          |
| `i16`   | signed     | 16                      | `[-2^15, 2^15 - 1]`                        |
| `i32`   | signed     | 32                      | `[-2^31, 2^31 - 1]`                        |
| `i64`   | signed     | 64                      | `[-2^63, 2^63 - 1]`                        |
| `i128`  | signed     | 128                     | `[-2^127, 2^127 - 1]`                      |
| `isize` | signed     | `8 × PointerSize`       | `[-2^(w-1), 2^(w-1) - 1]` for that width   |
| `u8`    | unsigned   | 8                       | `[0, 2^8 - 1]`                             |
| `u16`   | unsigned   | 16                      | `[0, 2^16 - 1]`                            |
| `u32`   | unsigned   | 32                      | `[0, 2^32 - 1]`                            |
| `u64`   | unsigned   | 64                      | `[0, 2^64 - 1]`                            |
| `u128`  | unsigned   | 128                     | `[0, 2^128 - 1]`                           |
| `usize` | unsigned   | `8 × PointerSize`       | `[0, 2^w - 1]` for that width              |

`isize`/`usize` are the pointer-sized integers; their width is `IntWidth = 8 × PointerSize`, where `PointerSize` is the target's pointer size in bytes. `usize` is the canonical type for array and slice lengths and indices (see §8.3 and §8.4).

The signed/unsigned partition is fixed by the spec sets:

```text
SignedIntTypes   = { i8, i16, i32, i64, i128, isize }
UnsignedIntTypes = { u8, u16, u32, u64, u128, usize }
IntTypes         = SignedIntTypes ∪ UnsignedIntTypes
```

#### 8.1.3 Floating-point types

There are exactly three floating types, each mapped to an IEEE 754-2019 binary format (`FloatFormat`, `FloatBitWidth`):

| Type  | IEEE 754-2019 format | Width (bits) | Canonical NaN bits     |
| ----- | -------------------- | ------------ | ---------------------- |
| `f16` | `binary16`           | 16           | `0x7E00`               |
| `f32` | `binary32`           | 32           | `0x7FC00000`           |
| `f64` | `binary64`           | 64           | `0x7FF8000000000000`   |

The value set of each float type (`FloatValueSet`) is exactly the values representable by its IEEE 754-2019 format. NaN encoding is canonicalised: every NaN value of type `t` encodes to `CanonicalNaNBits(t)`.

```text
FloatTypes   = { f16, f32, f64 }
NumericTypes = IntTypes ∪ FloatTypes
```

#### 8.1.4 `bool`, `char`, unit, never

- **`bool`** — the boolean type. Its only two values are the literals `true` and `false` (grammar: `bool_literal ::= "true" | "false"`). It lowers to a single byte: `true` is `0x01`, `false` is `0x00`.
- **`char`** — a single Unicode scalar value. A `char` literal is written between single quotes (`char_literal ::= "'" (char_content | escape_sequence) "'"`, where `char_content` is any Unicode scalar except `'`, `\`, or `U+000A`). It lowers to 4 bytes — the scalar's code point in little-endian order (`LEBytes(u, 4)`).
- **Unit type `()`** — spelled with an empty parenthesis pair, both as a type and as a value. It is the type of expressions that produce no meaningful value, and the default return type of a procedure with no `->` annotation (`ProcReturn(⊥) = ()`). The unit *value* is also written `()` (`unit_literal ::= "(" ")"`) and is the value of the empty tuple expression. It occupies zero bytes (`ValueBits(TypePrim("()"), ()) = []`).
- **Never type `!`** — spelled with a single `!`. It is the type of expressions that never produce a value (e.g. a diverging branch). It is well-formed as a type but has no values.

#### 8.1.5 Integer and float literal/type pairing rules

This is the precise rule set governing how a numeric literal acquires its type. It is the single most error-prone area for code that must compile, so it is reproduced verbatim. The literal grammar lives in the lexical section and is restated identically in Appendix B (B.1); the typing rules live in §16.1.4.

**Literal grammar:**

```ebnf
integer_literal  ::= (decimal_integer | hex_integer | octal_integer | binary_integer) int_suffix?
decimal_integer  ::= dec_digit ("_"* dec_digit)*
hex_integer      ::= "0x" hex_digit ("_"* hex_digit)*
octal_integer    ::= "0o" oct_digit ("_"* oct_digit)*
binary_integer   ::= "0b" bin_digit ("_"* bin_digit)*
int_suffix       ::= "i8" | "i16" | "i32" | "i64" | "i128" | "u8" | "u16" | "u32" | "u64" | "u128" | "isize" | "usize"

float_literal ::= decimal_integer "." decimal_integer? exponent? float_suffix?
exponent      ::= ("e" | "E") ("+" | "-")? decimal_integer
float_suffix  ::= "f" | "f16" | "f32" | "f64"
```

Underscores are permitted as digit separators (`1_000_000`, `0xFF_FF`). The integer suffix set is the full integer-type name set **including `isize` and `usize`** (`IntSuffixSet`); the float suffix set is `f`, `f16`, `f32`, `f64` (`FloatSuffixSet`).

**Integer literal typing.** A suffixed integer literal takes the suffix's type, provided the value is in range; an unsuffixed integer literal synthesizes `i32` (`DefaultInt = i32`):

```text
(T-Int-Literal-Suffix)
  lit.kind = IntLiteral    IntSuffix(lit) = t    InRange(IntValue(lit), t)
  ───────────────────────────────────────────────────────────────────────
  Γ ⊢ Literal(lit) : TypePrim(t)

(T-Int-Literal-Default)
  lit.kind = IntLiteral    IntSuffix(lit) = ⊥    InRange(IntValue(lit), `i32`)
  ───────────────────────────────────────────────────────────────────────────
  Γ ⊢ Literal(lit) : TypePrim(`i32`)
```

When an *expected* integer type is supplied by context, the checking rule **(Chk-Int-Literal)** checks an integer literal directly against that type as long as the value is in range:

```text
(Chk-Int-Literal)
  lit.kind = IntLiteral    T = TypePrim(t)    t ∈ IntTypes    InRange(IntValue(lit), t)
  ──────────────────────────────────────────────────────────────────────────────────────
  Γ; R; L ⊢ Literal(lit) ⇐ T ⊣ ∅
```

**Float literal typing.** An explicit width suffix fixes the type; a bare `f` suffix or no suffix infers from context and otherwise synthesizes `f32` (`DefaultFloat = f32`):

```text
(T-Float-Literal-Explicit)
  lit.kind = FloatLiteral    FloatSuffix(lit) = t    t ∈ { f16, f32, f64 }
  ─────────────────────────────────────────────────────────────────────────
  Γ ⊢ Literal(lit) : TypePrim(t)

(T-Float-Literal-Infer)
  lit.kind = FloatLiteral    (FloatSuffix(lit) = `f` ∨ NoFloatSuffix(lit))
  ─────────────────────────────────────────────────────────────────────────
  Γ ⊢ Literal(lit) : TypePrim(`f32`)
```

The corresponding checking rules **(Chk-Float-Literal-Explicit)** and **(Chk-Float-Literal-Infer)** check an explicitly-suffixed literal against the matching `f16/f32/f64` type, and an `f`-suffixed or unsuffixed float literal against any expected float type. A float literal must contain a decimal point (`1.0`, `2.5e3`, `0.5f64`); `1` is an integer literal. Using an explicit width suffix that conflicts with an expected type is an error (the explicit-check rule does not fire).

```ultraviolet
let count: i32 = 42              // unsuffixed integer literal, defaults to i32
let big: u64 = 4_000_000_000u64  // explicit suffix
let mask: u8 = 0b1010_1100       // unsuffixed, checked against expected u8
let offset: usize = 0            // unsuffixed, checked against expected usize
let ratio: f64 = 0.75            // unsuffixed float, checked against expected f64
let gain: f32 = 1.5f32           // explicit width suffix
let glyph: char = 'A'            // char literal
let visible: bool = true         // bool literal
let nothing: () = ()             // unit value
```

#### 8.1.6 Diagnostics

No primitive-type-specific diagnostic exists beyond well-formedness (`WF-Prim`); range and suffix mismatches are reported at the literal use sites governed by the rules above. A literal whose value falls outside the range of its suffixed or expected type is rejected because the `InRange` premise fails.

### 8.2 Tuples

A tuple is a fixed-arity, heterogeneous, positionally-indexed product type.

#### 8.2.1 Exact syntax

```ebnf
tuple_type       ::= "(" ")"
                   | "(" type ";" ")"
                   | "(" type ("," type)+ trailing_comma? ")"
tuple_expr       ::= "(" ")"
                   | "(" expr ";" ")"
                   | "(" expr ("," expr)+ trailing_comma? ")"
tuple_projection ::= postfix_expr "." int_literal
```

The Appendix B expression form (B.3) gives the literal as a single production, and B.3 spells projection as a `postfix_suffix`:

```ebnf
tuple_literal       ::= "(" tuple_expr_elements? ")"
tuple_expr_elements ::= expression ";" | expression ("," expression)+
postfix_suffix      ::= "." identifier | "." decimal_integer | "[" expression "]" | …
```

Key arity rules:

- `()` is the empty tuple — the unit value, of type `()`.
- A **one-element tuple** is written with a trailing semicolon inside the parentheses: `(T;)` for the type, `(e;)` for the value. The singleton *comma* forms `(T,)` and `(e,)` are **ill-formed**. A trailing comma denotes continuation only and never creates a one-element tuple (the parser's `TupleScanSingletonComma` rule explicitly rejects a lone comma before the closing paren).
- Two or more elements are comma-separated, with an optional trailing comma.

#### 8.2.2 Construction, indexing, arity

A tuple expression of `n ≥ 1` elements has the tuple type of the element types; the empty tuple expression has the unit type:

```text
(T-Tuple-Unit)
  ────────────────────────────────────
  Γ ⊢ TupleExpr([]) : TypePrim("()")

(T-Tuple)
  n ≥ 1    ∀ i, Γ ⊢ e_i : T_i
  ───────────────────────────────────────────────────────
  Γ ⊢ TupleExpr([e_1, …, e_n]) : TypeTuple([T_1, …, T_n])
```

**Tuple projection** uses `.` followed by an integer literal index (`tuple.0`, `tuple.1`). The index must be a compile-time constant integer literal (`ConstTupleIndex`) and must be in range `[0, n)`. The fields of a tuple `[T_1, …, T_n]` are numbered `0 … n-1` (`TupleFields`). Reading an element by value additionally requires that element's type to be `Bitcopy` (the `(T-Tuple-Index)` rule carries a `BitcopyType(T_i)` premise); a non-`Bitcopy` element is read by place/pattern rather than by-value projection.

```ultraviolet
let pair: (i32, bool) = (7, true)
let first: i32 = pair.0
let second: bool = pair.1

let triple: (f64, char, u8) = (3.14, 'x', 255u8)
let middle: char = triple.1

let singleton: (i32;) = (99;)    // one-element tuple, semicolon form
let only: i32 = singleton.0

let nothing: () = ()             // empty tuple == unit
```

#### 8.2.3 Diagnostics

| Code         | Condition                                                          |
| ------------ | ----------------------------------------------------------------- |
| `E-TYP-1801` | Tuple index out of bounds (`TupleIndex-OOB`)                      |
| `E-TYP-1802` | Tuple index is not a compile-time constant integer literal (`TupleIndex-NonConst`) |
| `E-TYP-1803` | Tuple arity mismatch in assignment or pattern (`Pat-Tuple-Arity-Err`) |
| `E-SEM-2524` | Tuple access on a non-tuple value (`TupleAccess-NotTuple`)        |

Because the projection index must be a literal, you cannot index a tuple with a runtime variable. Dynamic positional indexing is the job of arrays and slices (§8.3, §8.4). See the "Pattern Matching" chapter for destructuring tuples by pattern, which is the idiomatic way to bind all elements at once.

### 8.3 Arrays

An array `[T; N]` is a fixed-length, contiguous, homogeneous sequence whose length `N` is a compile-time constant.

#### 8.3.1 Exact syntax

```ebnf
array_type         ::= "[" type ";" expr "]"
array_expr         ::= "[" array_segment_list? "]"
array_segment_list ::= array_segment ("," array_segment)*
array_segment      ::= expr | expr ";" expr
index_expr         ::= postfix_expr "[" expr "]"
```

An array *type* is `[ElementType; LengthExpr]`. An array *literal* is a bracketed list of segments. A segment is either a single element `expr` or a **repeat segment** `expr ";" expr`, meaning "this value, repeated count times".

#### 8.3.2 Declaration, literals, length

The array type is well-formed when its length expression is a compile-time constant and its element type is well-formed:

```text
(WF-Array)
  T = TypeArray(T_0, e)    Γ ⊢ ConstLen(e) ⇓ n    Γ ⊢ T_0 wf
  ──────────────────────────────────────────────────────────
  Γ ⊢ T wf
```

An array literal's type is `TypeArray(T, N)` where `N` is the total length summed across all segments (`SegLen`: a single-element segment contributes 1; a repeat segment `value ; count` contributes the constant `count`). In a repeat segment the repeated value's type must be `Bitcopy`, the count must be a compile-time constant, and the count expression must type as an integer or `usize`:

```text
(T-Array-Literal-Segments)
  ∀ i,
    (s_i = ArrayElemSegment(value_i)        ⇒ Γ ⊢ value_i : T) ∧
    (s_i = ArrayRepeatSegment(value_i, count_i) ⇒
        Γ ⊢ value_i : T ∧ BitcopyType(T) ∧
        Γ ⊢ count_i : U_i ∧ (IntType(U_i) ∨ U_i = TypePrim("usize")) ∧
        Γ ⊢ ConstLen(count_i) ⇓ n_i)
  N = Σ_i SegLen(s_i)
  ────────────────────────────────────────────────────────────────────
  Γ ⊢ ArrayExpr([s_1, …, s_k]) : TypeArray(T, Literal(IntLiteral(N)))
```

```ultraviolet
let primes: [i32; 4] = [2, 3, 5, 7]          // four explicit elements
let zeros: [u8; 16] = [0u8; 16]              // repeat segment: 0u8 sixteen times
let mixed: [i32; 5] = [1, 2, 0; 3]           // 1, 2, then 0 repeated 3 times
let grid: [[u8; 3]; 2] = [[1u8, 2u8, 3u8], [4u8, 5u8, 6u8]]  // nested arrays
```

#### 8.3.3 Indexing and bounds

Array indexing is `array[index]`. The index expression must type as `usize`, or be an unsuffixed integer literal that fits `usize` (the `IndexUsizeExpr` relation). `IndexUsizeExpr` MUST NOT admit an explicitly suffixed non-`usize` literal, and a non-literal index must already have type `usize`.

There are two index regimes:

1. **Constant index.** If the index is a compile-time constant `i` (`ConstIndex`), the access is checked statically against the constant length `n`: it is well-formed iff `i < n`, and an out-of-bounds constant index is a compile-time error (`Index-Array-OOB-Err`).

2. **Dynamic index.** A non-constant index is only permitted inside a `#dynamic` context (`InDynamicContext`). Outside such a context, a non-constant array index is a compile-time error (`Index-Array-NonConst-Err`). Within a dynamic context, an out-of-bounds index **panics at runtime** (`EvalSigma-Index-OOB` produces `Ctrl(Panic)`).

```text
(P-Index-Array)
  Γ ⊢ e_1 :place TypeArray(T, len)    IndexUsizeExpr(e_2)    ConstIndex(e_2)
  Γ ⊢ ConstLen(e_2) ⇓ i    Γ ⊢ ConstLen(len) ⇓ n    i < n
  ─────────────────────────────────────────────────────────────────────────
  Γ ⊢ IndexAccess(e_1, e_2) :place T
```

```ultraviolet
let table: [i32; 4] = [10, 20, 30, 40]
let head: i32 = table[0]      // constant index, statically checked in bounds
let third: i32 = table[2]     // ok: 2 < 4
```

The array length `N` is a property of the type, fixed at compile time; arrays do not carry a runtime length word (a slice does — see §8.4).

#### 8.3.4 Layout

An array's size is `N × sizeof(T)`, and its alignment is `alignof(T)` (`Size-Array`, `Align-Array`). Elements are stored contiguously with no inter-element padding beyond each element's own size.

#### 8.3.5 Diagnostics

| Code         | Condition                                                                |
| ------------ | ----------------------------------------------------------------------- |
| `E-TYP-1810` | Array length is not a compile-time constant (`ConstLen-Err`)            |
| `E-TYP-1812` | Array index expression has non-`usize` type (`Index-Array-NonUsize`)   |

Non-constant array indexing outside a `#dynamic` scope (`Index-Array-NonConst-Err`) and out-of-bounds constant indices (`Index-Array-OOB-Err`) are also compile-time errors. Runtime out-of-bounds for dynamic indices panics through `EvalSigma-Index-OOB`.

### 8.4 Slices

A slice `[T]` is an unsized *view* into a contiguous run of `T` elements. It is a fat reference: a pointer plus a runtime length. Slices are produced by indexing an array or another slice with a range; they are not constructed directly with a literal.

#### 8.4.1 Exact syntax

```ebnf
slice_type ::= "[" type "]"
slice_expr ::= postfix_expr "[" expr "]"
```

The slice *type* is `[ElementType]` — bracketed element type with no length. The surface form for *producing* a slice is the same index-with-a-range postfix expression used for element indexing; whether `base[idx]` is a single-element access or a slice-producing selection depends on whether `idx` is a scalar index or a range. Array-to-slice coercion is semantic, not surface syntax.

#### 8.4.2 Views, length, sub-slicing

The slice type is well-formed when its element type is:

```text
(WF-Slice)
  T = TypeSlice(T_0)    Γ ⊢ T_0 wf
  ────────────────────────────────
  Γ ⊢ T wf
```

**Scalar indexing** of a slice (`slice[i]`) yields a single element of type `T`, with the same `usize`-index rule as arrays:

```text
(P-Index-Slice)
  Γ ⊢ e_1 :place TypeSlice(T)    IndexUsizeExpr(e_2)
  ──────────────────────────────────────────────────
  Γ ⊢ IndexAccess(e_1, e_2) :place T
```

**Sub-slicing** uses a range index. Indexing an array or slice with a range whose present bounds are `usize` produces a slice of the same element type. The accepted range index forms (`RangeIndexExpr`) are the full range `..`, `..end`, `..=end`, `start..`, `start..end`, and `start..=end`, with each present bound satisfying `IndexUsizeExpr`:

```text
(P-Slice-From-Array)
  Γ ⊢ e_1 :place TypeArray(T, n)    RangeIndexExpr(e_2)
  ─────────────────────────────────────────────────────
  Γ ⊢ IndexAccess(e_1, e_2) :place TypeSlice(T)

(P-Slice-From-Slice)
  Γ ⊢ e_1 :place TypeSlice(T)    RangeIndexExpr(e_2)
  ──────────────────────────────────────────────────
  Γ ⊢ IndexAccess(e_1, e_2) :place TypeSlice(T)
```

A slice's **length** is its number of elements, `end - start`, computed at runtime from the range bounds against the underlying length (`Len(SliceValue(v, r)) = end - start`). Sub-slice bounds must satisfy `0 ≤ start ≤ end ≤ L`; a range that violates this leaves `SliceBounds` undefined and **panics at runtime** (`EvalSigma-Index-Range-OOB`).

```ultraviolet
let data: [i32; 5] = [10, 20, 30, 40, 50]

let all: [i32] = data[..]          // full slice over the whole array
let head: [i32] = data[..3]        // elements 0,1,2  -> length 3
let tail: [i32] = data[2..]        // elements 2,3,4  -> length 3
let mid: [i32] = data[1..4]        // elements 1,2,3  -> length 3
let inc: [i32] = data[1..=3]       // elements 1,2,3 (inclusive end) -> length 3

let sub: [i32] = mid[1..]          // sub-slice of a slice
```

The exclusive vs inclusive distinction follows the range semantics in §8.5: `start..end` excludes `end`; `start..=end` includes `end` (the underlying upper bound is incremented by one via `Inc`).

#### 8.4.3 Layout

A slice value is a two-word fat reference: a pointer to the first element followed by a `usize` length. Its size is `2 × PtrSize` and its alignment is `PtrAlign` (`Size-Slice`, `Align-Slice`).

#### 8.4.4 Diagnostics

| Code         | Condition                                                              |
| ------------ | --------------------------------------------------------------------- |
| `E-TYP-1820` | Slice index expression has non-`usize` type (`Index-Slice-NonUsize`) |

Indexing a base that is neither an array nor a slice is `Index-NonIndexable`. Runtime slice-bounds failures panic: scalar out-of-bounds through `EvalSigma-Index-OOB`, range out-of-bounds through `EvalSigma-Index-Range-OOB`.

### 8.5 Ranges

A range is a value describing a bounded or half-bounded interval. Ranges are used both as standalone values and as slice selectors (§8.4).

#### 8.5.1 Exact syntax

```ebnf
range_expr      ::= ".."
                  | ".." expr
                  | "..=" expr
                  | expr ".."
                  | expr ".." expr
                  | expr "..=" expr
range_type_name ::= "Range" | "RangeInclusive" | "RangeFrom" | "RangeTo" | "RangeToInclusive" | "RangeFull"
```

The Appendix B expression productions (B.3) name each form:

```ebnf
exclusive_range    ::= logical_or_expr ".." logical_or_expr
inclusive_range    ::= logical_or_expr "..=" logical_or_expr
from_range         ::= logical_or_expr ".."
to_range           ::= ".." logical_or_expr
to_inclusive_range ::= "..=" logical_or_expr
full_range         ::= ".."
```

There are exactly six range expression forms and six corresponding range types. Range *types* use ordinary generic type syntax: `Range<T>`, `RangeInclusive<T>`, `RangeFrom<T>`, `RangeTo<T>`, `RangeToInclusive<T>`, and the nominal `RangeFull`. No range-specific type parser exists; the surface types elaborate to `TypeRange(T)`, `TypeRangeInclusive(T)`, `TypeRangeFrom(T)`, `TypeRangeTo(T)`, `TypeRangeToInclusive(T)`, and `TypeRangeFull` respectively.

#### 8.5.2 The six range forms and their types

| Surface form  | AST kind        | Synthesized type            | Bounds          |
| ------------- | --------------- | --------------------------- | --------------- |
| `start..end`  | `Exclusive`     | `Range<T>`                  | `[start, end)`  |
| `start..=end` | `Inclusive`     | `RangeInclusive<T>`         | `[start, end]`  |
| `start..`     | `From`          | `RangeFrom<T>`              | `[start, ∞)`    |
| `..end`       | `To`            | `RangeTo<T>`                | `(-∞, end)`     |
| `..=end`      | `ToInclusive`   | `RangeToInclusive<T>`       | `(-∞, end]`     |
| `..`          | `Full`          | `RangeFull`                 | everything      |

The element type `T` of a bounded range is the type of its bound expression(s); for two-bound forms both bounds must share the same type `T`:

```text
(Range-Exclusive)
  Γ; R; L ⊢ e_1 : T    Γ; R; L ⊢ e_2 : T
  ──────────────────────────────────────────────
  Γ; R; L ⊢ Range(`Exclusive`, e_1, e_2) : Range
```

The synthesized AST types follow directly:

```text
ExprType(Range(`Exclusive`, e_1, e_2))   = TypeRange(ExprType(e_1))
ExprType(Range(`Inclusive`, e_1, e_2))   = TypeRangeInclusive(ExprType(e_1))
ExprType(Range(`From`, e, ⊥))            = TypeRangeFrom(ExprType(e))
ExprType(Range(`To`, ⊥, e))              = TypeRangeTo(ExprType(e))
ExprType(Range(`ToInclusive`, ⊥, e))     = TypeRangeToInclusive(ExprType(e))
ExprType(Range(`Full`, ⊥, ⊥))            = TypeRangeFull
```

```ultraviolet
let half_open: Range<i32> = 0..10           // 0,1,…,9
let closed: RangeInclusive<i32> = 1..=5     // 1,2,3,4,5
let from_three: RangeFrom<usize> = 3..      // 3,4,5,…
let up_to: RangeTo<usize> = ..8             // up to but not including 8
let up_to_inc: RangeToInclusive<usize> = ..=8
let everything: RangeFull = ..
```

When a range is used directly as a slice selector its bounds are checked as `usize` index expressions (§8.4); used standalone it synthesizes its ordinary range type as above.

#### 8.5.3 Layout

The two-bound ranges (`Range<T>`, `RangeInclusive<T>`) lay out as a pair `[T, T]` (`TupleLayout([T, T])`). The one-bound ranges (`RangeFrom<T>`, `RangeTo<T>`, `RangeToInclusive<T>`) lay out as a single `T`. `RangeFull` is zero-sized (size 0, alignment 1).

#### 8.5.4 Diagnostics

Range-pattern diagnostics (non-constant and empty range patterns) are defined in the "Pattern Matching" chapter (§17.4). Slice-bounds failures induced by ranges panic at runtime when `SliceBounds` is undefined.

### 8.6 Records

A record is a named product type with named fields. It is the primary vehicle for plain value data, descriptors, configuration, and snapshots (style guide: use `record` for data-first structures, `class` only when shared identity or polymorphism is required).

#### 8.6.1 Exact syntax

The §12.6.1 grammar:

```ebnf
record_decl     ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? record_body type_invariant?
record_body     ::= "{" record_member* "}"
record_field    ::= attribute_list? visibility? key_boundary? identifier ":" type record_field_init_opt
key_boundary    ::= "#"
record_literal  ::= identifier "{" field_init_list "}"
default_record  ::= identifier "(" ")"
```

The Appendix B declaration grammar (B.6) gives the member and field-init productions and the `<:` implements clause:

```ebnf
record_decl       ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? "{" record_body "}" type_invariant?
record_body       ::= record_member*
record_member     ::= record_field_decl | method_def
record_field_decl ::= attribute_list? visibility? identifier ":" type record_field_init?
record_field_init ::= "=" expression
implements_clause ::= "<:" class_list
class_list        ::= type_path ("," type_path)*
```

And the literal/field-init expression grammar (B.3):

```ebnf
record_literal  ::= identifier "{" field_init_list "}" | state_specific_type "{" field_init_list? "}"
field_init_list ::= field_init ("," field_init)* ","?
field_init      ::= identifier ":" expression | identifier
```

A record declaration is `record Name { … }`. Members are fields and methods (and associated types). A field is `vis name: Type` with an optional `= default` initializer. The `#` **key boundary** marker (`key_boundary`) may prefix a field name. A record may declare generic parameters, an `<:` implements clause listing the classes it conforms to, and a trailing type invariant.

#### 8.6.2 Field visibility

Each field carries its own visibility (`public`, `internal`, `private`); always write it explicitly (style guide). A field's visibility may not exceed the record's own visibility:

```text
VisRank(`public`) = 3    VisRank(`internal`) = 2    VisRank(`private`) = 1
FieldVisOk(R) ⇔ ∀ f ∈ Fields(R). VisRank(f.vis) ≤ VisRank(R.vis)
```

Violating this is `FieldVisOk-Err` (`E-TYP-1906`). A field is *visible* at a use site when it is `public`/`internal`, or it is `private` and the use site is in the record's defining module:

```text
FieldVisible(m, R, f) ⇔ FieldVis(R, f) ∈ {`public`, `internal`}
                        ∨ (FieldVis(R, f) = `private` ∧ ModuleOfPath(RecordPath(R)) = m)
```

#### 8.6.3 Construction expressions

A **record literal** is `Name { field: value, … }`. It must:

- list each field name exactly once (no duplicates — `Distinct(FieldInitNames(fields))`);
- cover exactly the record's field set, no more and no fewer (`FieldInitSet(fields) = FieldNameSet(R)`);
- supply each value at the field's declared type, and only for fields visible at the call site.

```text
(T-Record-Literal)
  RecordDecl(p) = R    Distinct(FieldInitNames(fields))    FieldInitSet(fields) = FieldNameSet(R)
  ∀ ⟨f, e⟩ ∈ fields, FieldType(R, f) = T_f ∧ FieldVisible(m, R, f) ∧ Γ; R; L ⊢ e ⇐ T_f ⊣ ∅
  ────────────────────────────────────────────────────────────────────────────────────────────────
  Γ; R; L ⊢ RecordExpr(TypePath(p), fields) : TypePath(p)
```

The `field_init` grammar also allows the shorthand `identifier` (with no `: expression`), which initializes the field from a same-named binding in scope.

**Default construction.** The form `Name()` (the `default_record` production) constructs a record using each field's default initializer. It is well-formed only when *every* field has a default initializer (`DefaultConstructible`):

```text
DefaultConstructible(R) ⇔ ∀ f ∈ Fields(R). f.init_opt ≠ ⊥

(T-Record-Default)
  RecordCallee(callee) = R    Γ ⊢ R record wf    DefaultConstructible(R)
  ──────────────────────────────────────────────────────────────────────
  Γ ⊢ Call(callee, []) : TypePath(RecordPath(R))
```

If any field lacks a default, `Name()` is rejected with `Record-Default-Init-Err` (`E-TYP-1911`).

```ultraviolet
public record FrameConfig {
    public width: u32
    public height: u32
    public vsync: bool = true       // field with a default initializer
}

public record Origin {
    public x: f32 = 0.0f32
    public y: f32 = 0.0f32          // every field defaulted -> default-constructible
}

procedure makeConfigs() -> () {
    // Full record literal: every field named exactly once.
    let hd: FrameConfig = FrameConfig { width: 1920u32, height: 1080u32, vsync: false }

    // Field shorthand: initializes `width`/`height` from in-scope bindings.
    let width: u32 = 1280u32
    let height: u32 = 720u32
    let ready: FrameConfig = FrameConfig { width, height, vsync: true }

    // Default construction: allowed because every field has a default.
    let center: Origin = Origin()
}
```

#### 8.6.4 Field access

Read a field with `value.field` (the `Postfix-Field` form: `.` followed by an identifier). The field must be visible at the access site, and reading it by value requires the field type to be `Bitcopy`; the access yields the field's declared type:

```text
(T-Field-Record)
  Γ; R; L ⊢ e : TypePath(p)    RecordDecl(p) = R    FieldType(R, f) = T_f
  FieldVisible(m, R, f)    BitcopyType(T_f)
  ──────────────────────────────────────────────────────────────────────
  Γ; R; L ⊢ FieldAccess(e, f) : T_f
```

```ultraviolet
let hd: FrameConfig = FrameConfig { width: 1920u32, height: 1080u32, vsync: false }
let w: u32 = hd.width
let on: bool = hd.vsync
```

#### 8.6.5 On functional update

The specification defines **no** functional-update or "record update" syntax. There is no `..base` spread form and no `with { … }` form for records. To build a record from another, name the fields explicitly in a record literal. Do not invent spread syntax — it will not parse.

#### 8.6.6 Layout

Fields are laid out in declaration order (`Offsets`). Each field offset is the previous field's end rounded up to the next field's alignment (`AlignUp`); the record's alignment is the maximum field alignment (`RecordAlign`); the total size is the last field's end rounded up to the record alignment (`RecordSize`). An empty record has size 0 and alignment 1.

#### 8.6.7 Diagnostics

| Code         | Condition                                                                |
| ------------ | ------------------------------------------------------------------------ |
| `E-TYP-1901` | Duplicate field name in record declaration (`WF-Record-DupField`)       |
| `E-TYP-1902` | Missing field initializer in record literal (`Record-FieldInit-Missing`)|
| `E-TYP-1903` | Duplicate field initializer in record literal (`Record-FieldInit-Dup`)  |
| `E-TYP-1904` | Access to nonexistent field                                             |
| `E-TYP-1905` | Access to a field not visible in the current scope                      |
| `E-TYP-1906` | Field visibility exceeds record visibility (`FieldVisOk-Err`)           |
| `E-TYP-1907` | Non-`Bitcopy` field requires a move source expression (`Record-Field-NonBitcopy-Move`) |
| `E-TYP-1911` | Default record construction requires a default initializer for every field (`Record-Default-Init-Err`) |

### 8.7 Enums

An enum is a named sum type: a value is exactly one of a fixed set of variants. Variants may be unit (no payload), tuple-payload, or record-payload, and may carry explicit non-negative integer discriminants.

#### 8.7.1 Exact syntax

The §12.7.1 grammar:

```ebnf
enum_decl        ::= attribute_list? visibility? "enum" identifier generic_params? implements_clause? enum_body type_invariant?
enum_body        ::= "{" variant_members? "}"
variant_members  ::= variant (terminator variant)* terminator?
variant          ::= identifier variant_payload_opt variant_discriminant_opt
variant_payload  ::= "(" type_list? ")" | "{" field_decl_list? "}"
variant_literal  ::= qualified_variant | qualified_variant "(" arg_exprs? ")" | qualified_variant "{" field_init_list "}"
```

The Appendix B declaration grammar (B.6):

```ebnf
enum_decl       ::= attribute_list? visibility? "enum" identifier generic_params? implements_clause? "{" variant_members? "}" type_invariant?
variant_members ::= variant (terminator variant)* terminator?
variant         ::= identifier variant_payload? ("=" integer_literal)?
variant_payload ::= "(" type_list ")" | "{" field_decl_list "}"
```

And the literal form (B.3):

```ebnf
enum_literal ::= type_path "::" identifier variant_args?
variant_args ::= "(" expression_list ")" | "{" field_init_list "}"
```

**Critical separator rule:** top-level enum variants are item-separated members. Between top-level variants the implementation accepts **only** statement terminators — a newline or `;` (`terminator ::= ";" | newline`). A **comma must not** be used as an enum-variant separator. (Commas *are* used inside a variant's tuple or record payload.)

#### 8.7.2 Variant forms

- **Unit variant** — just a name: `Idle`.
- **Tuple-payload variant** — `Name(T_1, …, T_n)`: a positional payload.
- **Record-payload variant** — `Name { f: T, … }`: a named-field payload. Record-payload variant fields **may not** have default initializers; the AST enforces `∀ f ∈ RecordPayload. f.init_opt = ⊥`.

An enum must have at least one variant (`Enum-Empty-Err`), and variant names must be distinct (`Enum-Variant-Dup`).

```ultraviolet
public enum Shape {
    Empty
    Circle(f64)
    Rectangle(f64, f64)
    Labeled { name: string, sides: u32 }
}
```

Note the variants are separated by newlines, never commas.

#### 8.7.3 Discriminants and representation

A variant may carry an explicit discriminant: `= integer_literal`. The discriminant token must be an integer literal (`Enum-Disc-NotInt`), must denote a valid value (`Enum-Disc-Invalid`), and must be non-negative (`Enum-Disc-Negative`). Discriminants are assigned by the `DiscSeq` rule: a variant with no explicit discriminant takes the previous discriminant plus one, starting from 0. All resulting discriminants must be distinct (`Enum-Disc-Dup`).

The discriminant storage type (`DiscType(E)`) is the smallest unsigned integer that holds the maximum discriminant:

```text
DiscType(E) =
  u8   if 0 ≤ MaxDisc(E) ≤ 255
  u16  if 256 ≤ MaxDisc(E) ≤ 65,535
  u32  if 65,536 ≤ MaxDisc(E) ≤ 4,294,967,295
  u64  otherwise
```

An enum's layout is a tagged union: a discriminant of type `DiscType(E)` plus a payload area sized to the largest variant payload (`PayloadSize(E)`), aligned to the maximum of the discriminant alignment and the largest payload alignment (`EnumAlign(E)`).

```ultraviolet
public enum Priority {
    Low = 1
    Normal = 5
    High = 10           // explicit discriminants
}

public enum Direction {
    North               // discriminant 0
    East                // discriminant 1
    South               // discriminant 2
    West                // discriminant 3
}
```

#### 8.7.4 Construction

Construct a variant with the path `EnumName::Variant`, supplying payload arguments to match the variant's shape:

```text
(T-Enum-Lit-Unit)    EnumName::Variant                     where the variant is a unit variant
(T-Enum-Lit-Tuple)   EnumName::Variant(e_1, …, e_n)        where it is a tuple-payload variant
(T-Enum-Lit-Record)  EnumName::Variant { f: e, … }         where it is a record-payload variant
```

For a tuple-payload variant the argument count must equal the payload arity (`Enum-Lit-Tuple-Arity-Err` otherwise). For a record-payload variant every payload field must be initialized exactly once (`Enum-Lit-Record-MissingField` otherwise). An unknown variant name is `Enum-Lit-Unknown`.

```ultraviolet
let nothing: Shape = Shape::Empty
let unit_circle: Shape = Shape::Circle(1.0)
let area: Shape = Shape::Rectangle(3.0, 4.0)
let tile: Shape = Shape::Labeled { name: "tile", sides: 4u32 }

let urgent: Priority = Priority::High
```

To read the payload out of an enum value, match on it; see the "Pattern Matching" chapter.

#### 8.7.5 Diagnostics

| Code         | Condition                                                                |
| ------------ | ------------------------------------------------------------------------ |
| `E-TYP-1920` | Enum discriminant is not an integer literal (`Enum-Disc-NotInt`)        |
| `E-TYP-1921` | Enum discriminant literal is invalid (`Enum-Disc-Invalid`)              |
| `E-TYP-1922` | Enum discriminant must be non-negative (`Enum-Disc-Negative`)           |
| `E-TYP-1923` | Duplicate enum discriminant value (`Enum-Disc-Dup`)                      |
| `E-TYP-2001` | Enum declaration contains no variants (`Enum-Empty-Err`)                |
| `E-TYP-2002` | Duplicate variant name in enum declaration (`Enum-Variant-Dup`)         |
| `E-TYP-2007` | Unknown variant name in enum construction (`Enum-Lit-Unknown`)          |
| `E-TYP-2008` | Variant payload arity mismatch (`Enum-Lit-Tuple-Arity-Err`)             |
| `E-TYP-2009` | Missing field initializer in record-like variant (`Enum-Lit-Record-MissingField`) |

### 8.8 Union Types

A union type `A | B | …` is a structural sum of two or more member types. Unlike an enum, a union has no nominal name and no explicit variant labels — membership is by type. Union introduction is *semantic*: any expression whose type is a member of a union may be typed as that union.

#### 8.8.1 Exact syntax

```ebnf
union_type ::= non_union_type ("|" non_union_type)+
```

A union is written as member types separated by `|`, with at least two members. Each member must be a `non_union_type` (unions do not directly nest in surface syntax; parenthesize where the grammar requires it, e.g. a union parameter type inside a `closure_type` or `closure_expr` MUST be parenthesized as `( type )`). Union types also appear as procedure return types via `union_return ::= type ("|" type)+`.

#### 8.8.2 Semantics

A union type is well-formed only with two or more members (`WF-Union`); fewer than two members is `WF-Union-TooFew` (`E-TYP-2201`):

```text
(WF-Union)
  T = TypeUnion([T_1, …, T_n])    n ≥ 2    ∀ i, Γ ⊢ T_i wf
  ────────────────────────────────────────────────────────
  Γ ⊢ T wf
```

A value of any member type can be given the union type (subsumption):

```text
Member(T, U) ⇔ U = TypeUnion([U_1, …, U_n]) ∧ ∃ i. Γ ⊢ T ≡ U_i

(T-Union-Intro)
  Γ ⊢ e : T    Member(T, U)
  ──────────────────────────
  Γ ⊢ e : U
```

You **cannot** directly access a member's fields or operations through a union value without first refining it — direct field access on a union value is `Union-DirectAccess-Err` (`E-TYP-2202`). To use a union value you discriminate it with `if … is { … }` case analysis (defined in the control-flow / pattern chapters); the union's exhaustiveness rules are checked there.

```ultraviolet
public record IoFailure {
    public code: i32
}

// A value that is either an integer code or an error record.
type Outcome = i32 | IoFailure

procedure classify(value: i32) -> i32 | IoFailure {
    if value < 0 {
        return IoFailure { code: value }
    }
    return value          // i32 is a member of the return union, so this is admitted
}
```

#### 8.8.3 Layout

A union lays out as a tagged union: a discriminant whose type is `UnionDiscType(U) = DiscType(|members| - 1)` plus a payload area sized and aligned to the largest member. When exactly one member carries a payload with enough spare bit patterns and the remaining members are all unit-like, a niche representation (`Layout-Union-Niche`) is used instead — the payload member's niche values encode the empty members, eliminating the explicit tag.

#### 8.8.4 Diagnostics

| Code         | Condition                                                                |
| ------------ | ------------------------------------------------------------------------ |
| `E-TYP-2201` | Union type has fewer than two member types (`WF-Union-TooFew`)          |
| `E-TYP-2202` | Direct access on a union value without pattern matching (`Union-DirectAccess-Err`) |

Exhaustiveness diagnostics for union `if … is { … }` analysis are defined in the "Pattern Matching" chapter (§17.6).

### 8.9 Type Aliases

A type alias gives an existing type a new name. It introduces no new runtime type and no distinct values — it is transparent: the alias and its body are the same type after normalization.

#### 8.9.1 Exact syntax

```ebnf
type_alias_decl ::= attribute_list? visibility? "type" identifier generic_params? "=" type
```

A type alias is `type Name = SomeType`. It may be generic; it has no implements clause and no body block.

#### 8.9.2 Semantics

Aliases are resolved by normalization (`AliasNorm`), which recursively rewrites an aliased path to its body through every type former (tuples, arrays, slices, unions, function types, generic applications, ranges, pointers, refinements, modal-state refs, etc.). Two types are alias-transparent equal when they normalize to the same type (`AliasTransparent(T, U) ⇔ AliasNorm(T) = AliasNorm(U)`). Because aliases are transparent, an alias may be used anywhere its body type is expected, and vice versa.

An alias is well-formed when its body is well-formed and the alias is **not recursive**: an alias whose body refers (directly or transitively) back to itself through the `AliasGraph` is rejected with `TypeAlias-Recursive-Err` (`AliasCycle(p)`).

A type alias introduces no distinct runtime values; its size, alignment, layout, and bit representation are exactly those of its body:

```text
(Size-Alias)
  T = TypePath(p)    AliasBody(p) = ty    Γ ⊢ sizeof(ty) = size
  ─────────────────────────────────────────────────────────────
  Γ ⊢ sizeof(T) = size
```

```ultraviolet
public record IoFailure {
    public code: i32
}

type Pixel = u32                         // simple alias
type Rgba = (u8, u8, u8, u8)             // alias for a tuple type
type Matrix = [[f32; 4]; 4]              // alias for a nested array type
type Handle = i32 | IoFailure            // alias for a union type

procedure toRed(packed: Rgba) -> u32 {
    // `Rgba` is transparently the tuple type, so projection works directly.
    let red: u8 = packed.0
    return red as u32
}
```

Generic aliases parameterize the body (generic type parameters use the `T`-prefix `PascalCase` style):

```ultraviolet
type Pair<TValue> = (TValue, TValue)

procedure originPair() -> Pair<f32> {
    return (0.0f32, 0.0f32)
}
```

#### 8.9.3 Diagnostics

Recursive type aliases are rejected (`TypeAlias-Recursive-Err`). Generic-argument count and bound failures for alias applications are reported by the shared type-application rules in the "Generics" chapter (Chapter 14).

### 8.10 Shared Data-Type Diagnostics

The specification consolidates several array/slice/union diagnostics in §12.10. They are restated here for reference:

| Code         | Condition                                                              |
| ------------ | --------------------------------------------------------------------- |
| `E-TYP-1810` | Array length is not a compile-time constant (`ConstLen-Err`)         |
| `E-TYP-1812` | Array index expression has non-`usize` type (`Index-Array-NonUsize`) |
| `E-TYP-1820` | Slice index expression has non-`usize` type (`Index-Slice-NonUsize`) |
| `E-TYP-2201` | Union type has fewer than two member types (`WF-Union-TooFew`)        |
| `E-TYP-2202` | Direct access on a union value without pattern matching (`Union-DirectAccess-Err`) |

### Idioms & Best Practices

- **Use `record` for data, not `class`.** Records are the default for plain value data, descriptors, configuration, snapshots, and other data-first structures. Reserve `class` for shared identity, polymorphism, or reference-oriented behavior, and `modal` for lifecycle/state-based types (style guide, Type Design).
- **Name fields by convention.** Public and internal instance fields are `snake_case` (`package_id`, `world_id`); private instance fields are `_snake_case` (`_device`, `_frame_index`). Boolean fields read as predicates (`is_ready`, `has_focus`, `should_reload`). Types — records, enums, classes, modals, and type aliases — are `PascalCase`; enum variants are `PascalCase` (`Windowed`, `BorderlessFullscreen`); generic type parameters are `PascalCase` with a `T` prefix (`TValue`, `TState`).
- **Always write field and type visibility explicitly.** Treat visibility as part of the API contract; do not rely on omitted defaults. Keep field visibility no wider than the record's visibility (the compiler enforces this via `FieldVisOk`).
- **Prefer `usize` for lengths and indices.** Array and slice indices must be `usize` (or an unsuffixed literal that fits `usize`). Declare index and length variables as `usize` rather than narrower integer types to avoid `Index-Array-NonUsize` / `Index-Slice-NonUsize` errors.
- **Default integer literals to `i32`, floats to `f32` — but pin the type where it matters.** When a value's width is significant (counts that may exceed `i32`, byte values, pointer-sized offsets), use an explicit suffix or an explicitly typed binding so the literal is checked against the type you intend.
- **Model closed sets as enums; model open structural choices as unions; model the error channel with `Outcome`.** An enum is a named, labeled, exhaustive set of cases ideal for protocol states and tagged data. A union is anonymous and structural, ideal for a "this OR that type" choice among distinct existing types. For fallible results specifically, return `Outcome<T, E>` — a two-variant enum (`Value(T)` / `Error(E)`) that is tag-discriminated, so it works even when `T` and `E` overlap and composes with `?` — rather than a bare `T | E` union. Prefer enums when you want named variants and discriminants; prefer unions when the alternatives are existing types you do not want to re-wrap.
- **Use type aliases to name intent, not to hide complexity.** A `type Rgba = (u8, u8, u8, u8)` clarifies a tuple's meaning at use sites. Because aliases are transparent, they document without adding a conversion boundary. Keep aliases acyclic.
- **Destructure tuples and match enums by pattern.** Tuple projection (`.0`) and enum construction give you values; reading multiple tuple elements or an enum payload is idiomatic via patterns (see "Pattern Matching"), which is clearer than repeated indexing and required when an element type is not `Bitcopy`.
- **Reach for slices to pass borrowed sequence views.** When a procedure should accept "some run of `T`" regardless of the backing array length, take a slice `[T]` parameter and pass `array[..]` or a sub-range at the call site, rather than fixing an array length in the signature.

### Pitfalls & Diagnostics

- **The unit type is `()`, never `unit`.** Both the type and the value are spelled `()`. A procedure with no result is annotated `-> ()` (or the `->` clause is omitted entirely). There is no `unit`, `void`, `int`, `float`, or `byte` keyword.
- **One-element tuples use a semicolon, never a comma.** Write `(x;)` and `(T;)`. The forms `(x,)` and `(T,)` are ill-formed; a trailing comma is continuation only and does not create a singleton tuple.
- **Enum variants are separated by newlines or `;`, never commas.** A comma between top-level variants is rejected by the parser. Commas appear only *inside* a variant's `(…)` tuple payload or `{…}` record payload.
- **Tuple indices must be constant literals.** `tuple.0` is fine; `tuple.i` for a variable `i` is `TupleIndex-NonConst` (`E-TYP-1802`). For runtime positional access use an array or slice.
- **Constant array indices are bounds-checked at compile time.** `table[5]` on a `[i32; 4]` is `Index-Array-OOB-Err`. A *non-constant* array index is only legal inside a `#dynamic` scope; otherwise it is `Index-Array-NonConst-Err`. Inside a dynamic scope, an out-of-range index panics at runtime.
- **Record literals must name every field exactly once.** Missing a field is `Record-FieldInit-Missing` (`E-TYP-1902`); naming one twice is `Record-FieldInit-Dup` (`E-TYP-1903`). There is no partial-construction or spread syntax to fill the rest.
- **`Name()` default construction requires every field to have a default.** If even one field lacks an `= default` initializer, `Name()` is `Record-Default-Init-Err` (`E-TYP-1911`). Use a full record literal instead.
- **Record-payload enum variant fields cannot have defaults.** Unlike record declarations, a record-like variant's fields must omit initializers; the AST enforces `f.init_opt = ⊥` for every variant field.
- **You cannot read through a union without refining it.** Accessing a field or method on a union value directly is `Union-DirectAccess-Err` (`E-TYP-2202`). Discriminate with `if … is { … }` first. A union also needs at least two members (`WF-Union-TooFew`, `E-TYP-2201`).
- **Field visibility cannot exceed record visibility.** A `public` field inside an `internal` record is `FieldVisOk-Err` (`E-TYP-1906`). Private fields are only visible inside the record's own module.
- **An `if` always takes a block.** The grammar is `if expression block_expr (else …)?`. There is no brace-less `if cond return e` form — write `if cond { return e }`.
- **By-value field and tuple projection require `Bitcopy`.** Reading `record.field` or `tuple.0` *by value* carries a `BitcopyType` premise; for a non-`Bitcopy` element, bind it by place or destructure it with a pattern instead.
- **Type aliases must not be recursive.** `type Loop = Loop` (or any cycle through the alias graph) is `TypeAlias-Recursive-Err`. Aliases are transparent names, not new recursive types — use an `enum` or `record` (which can be self-referential through indirection) for recursive shapes.
- **Float literals need a decimal point.** `1.0`, `2.5e3`, `0.5f64` are float literals; `1` is an integer literal. Pairing an explicit float width suffix with a conflicting expected type is an error, as is using a suffix on a value out of that type's range (the `InRange` premise fails).
