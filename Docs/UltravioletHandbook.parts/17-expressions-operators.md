## 17. Expressions & Operators

Expressions are the value-producing core of Ultraviolet. Every operand of an
operator, every argument to a procedure, every condition of an `if`, and every
right-hand side of a binding is an expression. This chapter specifies each
expression form defined by §16 of the language specification: its exact syntax,
its static and dynamic semantics, and at least one idiomatic worked example.

The chapter is organized to match the specification: literal and name
expressions (§16.1), access and place expressions (§16.2), call expressions
(§16.3), operator expressions (§16.4), cast and transmute expressions (§16.5),
construction expressions (§16.6), control expressions (§16.7), the effectful
core expressions (§16.8), and finally closures and pipelines (§16.9). The
operator material (§17.5) is the load-bearing reference for any code that
decodes bits and bytes: it fixes the exact tokens, precedence, associativity,
and integer-width behavior of every arithmetic, comparison, logical, bitwise,
and shift operator.

Related chapters: primitive type names are covered in the Types chapter;
patterns used by `if ... is` and loop bindings are covered in the Patterns
chapter; statements, blocks, bindings, and assignment are covered in the
Statements chapter; capabilities, permissions, and method receivers are covered
in the Capabilities chapter.

### 17.1 Operator Precedence and Associativity (Overview)

The expression grammar is a precedence cascade: each production layer binds more
tightly than the one above it. The following block is the consolidated
expression grammar reproduced verbatim from Appendix B.3 of the specification.
It is the authoritative ordering; where the per-section grammar of §16.4.1 omits
`cast_expr` and the prefix forms `& * move widen` (deferring them to §16.5,
§16.8, and §13.5), Appendix B.3 places them in their true cascade positions.

```ebnf
expression         ::= attributed_expr | unattributed_expr
attributed_expr    ::= attribute_list expression
unattributed_expr  ::= range_expression | logical_or_expr

range_expression    ::= exclusive_range | inclusive_range | from_range | to_range | to_inclusive_range | full_range
exclusive_range     ::= logical_or_expr ".." logical_or_expr
inclusive_range     ::= logical_or_expr "..=" logical_or_expr
from_range          ::= logical_or_expr ".."
to_range            ::= ".." logical_or_expr
to_inclusive_range  ::= "..=" logical_or_expr
full_range          ::= ".."

logical_or_expr     ::= logical_and_expr ("||" logical_and_expr)*
logical_and_expr    ::= comparison_expr ("&&" comparison_expr)*
comparison_expr     ::= bitwise_or_expr (comparison_op bitwise_or_expr)*
comparison_op       ::= "==" | "!=" | "<" | "<=" | ">" | ">="
bitwise_or_expr     ::= bitwise_xor_expr ("|" bitwise_xor_expr)*
bitwise_xor_expr    ::= bitwise_and_expr ("^" bitwise_and_expr)*
bitwise_and_expr    ::= shift_expr ("&" shift_expr)*
shift_expr          ::= additive_expr (shift_op additive_expr)*
shift_op            ::= "<<" | ">>"
additive_expr       ::= multiplicative_expr (additive_op multiplicative_expr)*
additive_op         ::= "+" | "-"
multiplicative_expr ::= power_expr (multiplicative_op power_expr)*
multiplicative_op   ::= "*" | "/" | "%"
power_expr          ::= cast_expr ("**" power_expr)?
cast_expr           ::= unary_expr ("as" type)?

unary_expr     ::= new_expr | unary_operator unary_expr | pipeline_expr
new_expr       ::= "new" unary_expr
unary_operator ::= "!" | "-" | "&" | "*" | "move" | "copy" | "widen"
pipeline_expr  ::= postfix_expr ("=>" postfix_expr)*

postfix_expr   ::= primary_expr postfix_suffix*
postfix_suffix ::= "." identifier | "." decimal_integer | "[" expression "]" | "~>" identifier "(" argument_list? ")" | "(" argument_list? ")" | "?"
```

From this cascade, precedence runs from **lowest** (loosest binding) to
**highest** (tightest binding):

| Level | Operators (tokens) | Associativity |
| ----- | ------------------ | ------------- |
| 1 (lowest) | range: `..`, `..=` | non-associative (single bound pair) |
| 2 | logical or: `\|\|` | left |
| 3 | logical and: `&&` | left |
| 4 | comparison: `==` `!=` `<` `<=` `>` `>=` | left-chained by grammar (each is a separate `bool` op) |
| 5 | bitwise or: `\|` | left |
| 6 | bitwise xor: `^` | left |
| 7 | bitwise and: `&` | left |
| 8 | shift: `<<` `>>` | left |
| 9 | additive: `+` `-` | left |
| 10 | multiplicative: `*` `/` `%` | left |
| 11 | power: `**` | **right** |
| 12 | cast: `as` | postfix (one type) |
| 13 | prefix unary: `!` `-` `&` `*` `move` `widen` | prefix |
| 14 | pipeline: `=>` | left |
| 15 (highest) | postfix: `.field` `.index` `[...]` `~>m(...)` `(...)` `?` | left |

Two consequences of this layout matter constantly when writing bit/byte code:

- Bitwise operators bind **looser** than comparison. `x & MASK == 0` parses as
  `x & (MASK == 0)`, which is ill-typed. Write `(x & MASK) == 0`.
- Shift binds **tighter** than bitwise `& | ^` but **looser** than additive.
  `1 << n + 1` parses as `1 << (n + 1)`. Parenthesize when in doubt.

`**` (power) is the only **right-associative** binary operator: `2 ** 3 ** 2`
is `2 ** (3 ** 2)`. Because `power_expr`'s left operand is `cast_expr`, the cast
operator `as` binds **tighter** than `**`.

### 17.2 Literal and Name Expressions (§16.1)

#### 17.2.1 Syntax

```ebnf
literal_expr        ::= literal
null_ptr_expr       ::= "Ptr" "::" "null" "(" ")"
identifier_expr     ::= identifier
qualified_name_expr ::= path "::" identifier

literal ::= integer_literal | float_literal | string_literal
          | char_literal | bool_literal | null_literal | unit_literal
```

A bare `identifier` is a name expression only when it is **not** followed by
`::`, `@`, or `{` (those introduce qualified names, modal/state forms, and
record literals respectively). A `path "::" identifier` not followed by `(`,
`{`, or `@` is a qualified name expression.

#### 17.2.2 Integer literals

Integer literals are written in decimal, hexadecimal (`0x`), octal (`0o`), or
binary (`0b`), with optional `_` digit separators, and an optional integer type
suffix:

```ebnf
integer_literal ::= (decimal_integer | hex_integer | octal_integer | binary_integer) int_suffix?
int_suffix      ::= "i8" | "i16" | "i32" | "i64" | "i128"
                  | "u8" | "u16" | "u32" | "u64" | "u128" | "isize" | "usize"
```

Typing (§16.1.4):

- A **suffixed** integer literal has the type named by its suffix, and its
  value MUST be in range for that type — **(T-Int-Literal-Suffix)**.
- An **unsuffixed** integer literal defaults to `i32` and its value MUST be in
  range for `i32` — **(T-Int-Literal-Default)** (`DefaultInt = i32`).
- In checking mode, an unsuffixed integer literal may take any expected integer
  type `t ∈ IntTypes` as long as `InRange(IntValue(lit), t)` — **(Chk-Int-Literal)**.

The full set of integer types is fixed:

```text
IntTypes = { i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize }
SignedIntTypes   = { i8, i16, i32, i64, i128, isize }
UnsignedIntTypes = { u8, u16, u32, u64, u128, usize }
```

```ultraviolet
let red_mask: u32 = 0x00FF_0000u32
let lane_count: usize = 8usize
let signed_default = -1          // i32 by default
let big: u64 = 0b1010_1010_1111u64
```

#### 17.2.3 Float, bool, char, string, unit, and null literals

```ebnf
float_literal ::= decimal_integer "." decimal_integer? exponent? float_suffix?
exponent      ::= ("e" | "E") ("+" | "-")? decimal_integer
float_suffix  ::= "f" | "f16" | "f32" | "f64"
bool_literal  ::= "true" | "false"
null_literal  ::= "null"
unit_literal  ::= "(" ")"
```

Typing (§16.1.4):

- A float literal suffixed `f16`/`f32`/`f64` has exactly that type
  (**T-Float-Literal-Explicit**). A float literal with suffix `f` or **no**
  suffix defaults to `f32` (**T-Float-Literal-Infer**; `DefaultFloat = f32`).
  In checking mode it may take any expected `t ∈ FloatTypes`
  (`FloatTypes = { f16, f32, f64 }`).
- `true`/`false` have type `bool` (**T-Bool-Literal**).
- A char literal has type `char` (**T-Char-Literal**).
- A string literal has type `string@View` (**T-String-Literal**, written
  `TypeString(@View)`). The `@`-state forms of `string` are `Managed` and `View`.
- `null` is checked against an expected **raw pointer** type `*imm T` / `*mut T`
  (**Chk-Null-Literal**); it has no synthesized type on its own.
- `()` is the unit literal of type `()` (the unit type), produced as
  `TupleExpr([])` (§16.6.4).

The null **safe pointer** is a separate form, `Ptr::null()`, which evaluates to
`Ptr@Null(0x0)` (**EvalSigma-PtrNull**). Do not confuse it with the `null`
literal, which is for raw pointers.

```ultraviolet
let gravity: f32 = 9.81f32
let ratio = 0.5                  // f32 by default
let is_ready: bool = true
let newline: char = '\n'
let empty_handle: Ptr<i32> = Ptr::null()
```

#### 17.2.4 Name expressions

A bare `identifier` reads a local binding or parameter `x` whose type `T` is in
scope and is `Bitcopy` (**T-Ident**). A `path::name` reads a value-bearing path
such as a module-scope constant or a zero-generic procedure used as a value
(**T-Path-Value**). Before name resolution a `path::name` is the pre-resolution
form `QualifiedName(path, name)`; resolution rewrites it to a resolved `Path`
or, for a unit enum variant, to an `EnumLiteral`. An unresolved qualified name
is rejected (**Expr-Unresolved-Err**).

```ultraviolet
let count = frame_index          // local read
let limit = Config::MAX_SUBTICKS // module-scope constant path
```

### 17.3 Access and Place Expressions (§16.2)

#### 17.3.1 Syntax

```ebnf
access_suffix ::= "." identifier | "." decimal_literal | "[" expression "]"
place_expr    ::= "*" place_expr | postfix_expr
```

The three access suffixes are **field access** (`.name`), **tuple index**
(`.0`, `.1`, …), and **indexing** (`[expr]`). Combined with `*` dereference and
bare identifiers, these are the **place expressions** — expressions that denote
a storage location, not just a value:

```text
IsPlace(e) ⇔ e ∈ { Identifier(_), FieldAccess(_, _), TupleAccess(_, _), IndexAccess(_, _) }
           ∨ (∃ p. e = Deref(p) ∧ IsPlace(p))
```

A **place** is required wherever the language needs an addressable location:
the left side of assignment, the operand of `&` (address-of), and the operand
of `move`. A **value** expression is any expression; every place can be read as
a value, but not every value is a place. Address-of and move are specified in
§17.9.

#### 17.3.2 Field access

`FieldAccess(e, f)` reads field `f` of a record (**T-Field-Record**). The field
must exist, must be **visible** from the current module `m` (`public`/`internal`
always; `private` only within the declaring module), and its type must be
`Bitcopy` to be read by value:

```text
FieldVisible(m, R, f) ⇔ FieldVis(R, f) ∈ { public, internal }
                      ∨ (FieldVis(R, f) = private ∧ ModuleOfPath(RecordPath(R)) = m)
```

A permission on the base (`const`/`unique`/`shared`) propagates to the field
result (**T-Field-Record-Perm**). Field access is also a **place**
(**P-Field-Record**), so `r.field = x` and `&r.field` are valid. Direct field
access on a **union**-typed base is rejected (**Union-DirectAccess-Err**);
narrow a union with `if ... is` first.

```ultraviolet
let speed: f32 = frame_request.frame_delta
```

#### 17.3.3 Tuple index

`TupleAccess(e, i)` reads element `i` of a tuple, where `i` is a **decimal
integer literal** index (**T-Tuple-Index**). The index must be in `0 ≤ i < n`
for an `n`-element tuple; non-constant or out-of-range tuple indices are
rejected. Tuple index is also a place form (**P-Tuple-Index**).

```ultraviolet
let pair: (i32, f32) = (7, 2.5f32)
let whole: i32 = pair.0
let frac: f32 = pair.1
```

#### 17.3.4 Indexing and slicing

`IndexAccess(e1, e2)` indexes arrays and slices. The rules (§16.2.4):

- **Scalar index** of an array `[T; len]` or a slice `[T]` yields `T`. The index
  expression must satisfy `IndexUsizeExpr`: a direct unsuffixed integer literal
  is contextually checked as `usize`, while a non-literal index must already
  type as `usize`.
- For a fixed-size array, a **constant** scalar index is bounds-checked at
  compile time (**T-Index-Array**); a non-constant array index is permitted
  only inside a `#dynamic` context (**T-Index-Array-Dynamic**). Slice indices
  need not be constant (**T-Index-Slice**).
- A **range index** produces a slice: indexing an array or slice with a range
  expression yields `[T]` (**T-Slice-From-Array**, **T-Slice-From-Slice**). The
  admissible range carriers are the `usize`-typed ranges (`RangeIndexType`).
- A permission on the base propagates to the element/slice (the `-Perm`
  variants).
- An array `[T; n]` coerces to a slice `[T]` of the same permission
  (**Coerce-Array-Slice**).

Indexing is also a place form (the `P-` variants), so `buffer[i] = v` is valid.
Out-of-bounds scalar access and out-of-bounds range slicing lower to a panic at
run time.

```ultraviolet
let bytes: [u8; 4] = [0xDE, 0xAD, 0xBE, 0xEF]
let first: u8 = bytes[0]          // constant index, compile-time checked
let tail: [u8] = bytes[1..4]      // range index produces a slice [u8]

procedure readByte(buffer: [u8], offset: usize) -> u8 {
    return buffer[offset]         // slice index: offset already usize
}
```

### 17.4 Call Expressions (§16.3)

#### 17.4.1 Syntax

```ebnf
call_expr         ::= postfix_expr "(" argument_list? ")"
generic_call_expr ::= postfix_expr generic_args "(" argument_list? ")"
method_call_expr  ::= postfix_expr "~>" identifier "(" argument_list? ")"
argument_list     ::= argument ("," argument)* ","?
argument          ::= ("move" | "copy")? expression
```

There are three call forms: an **ordinary call** `callee(args)`, an explicit
**type-argument call** `callee<T...>(args)`, and a **method / capability call**
`receiver~>name(args)` using the `~>` operator. Each argument may carry an
explicit `move` or `copy` pass-kind.

#### 17.4.2 Ordinary calls and argument passing

An ordinary `Call(callee, args)` requires the callee to have a function type
`TypeFunc(params, R_c)`; the call has the return type `R_c` (**T-Call**). The
argument count must match the parameter count, and each argument must be
type-compatible (`<:`) with its parameter.

Argument pass-kinds interact with parameter modes (§16.3.4). A parameter is
either **consuming** (mode `move`) or non-consuming (mode `⊥`):

- A **consuming** parameter requires the argument to transfer ownership. Pass it
  with `move` on a place, or with `copy` to supply a duplicate. Passing a place
  by plain reference to a consuming parameter is rejected
  (**Call-Move-Missing**, `E-SEM-2534`).
- A non-consuming parameter takes the argument by reference. Adding `move` to
  such an argument is rejected (**Call-Move-Unexpected**, `E-SEM-2535`).
- `copy e` requires `e` to be `Bitcopy` (`E-UNS-0107`).
- A by-reference argument must be a **place** (**Call-Arg-NotPlace**); a
  by-reference argument that is a `layout(packed)` record field requires an
  `unsafe` context (**Call-Arg-Packed-Unsafe-Err**).

Diagnostics: non-callable callee (`E-SEM-2531`), arity mismatch
(`E-SEM-2532`), argument type mismatch (`E-SEM-2533`).

```ultraviolet
procedure scaleSample(sample: i16, gain: i16) -> i16 {
    return sample * gain
}

procedure consume(buffer: move ByteBuffer) -> usize {
    // buffer is owned here
    return buffer.length
}

procedure run(owned: move ByteBuffer, src: ByteBuffer) {
    let scaled: i16 = scaleSample(120i16, 2i16)   // by-reference args
    let used: usize = consume(move owned)         // ownership transfer
    let dup: usize  = consume(copy src)           // explicit duplicate
}
```

A record whose every field has a default initializer is
**default-constructible** and may be built with zero-argument call syntax
`Record()` (**T-Record-Default**, §16.6.4); if any field lacks a default this is
rejected (**Record-Default-Init-Err**).

#### 17.4.3 Generic calls

`callee<T...>(args)` supplies explicit type arguments
(`CallTypeArgs(callee, targs, args)`). When type arguments are omitted on a
generic callee, they are inferred from the argument and expected types
(**T-Call-Generic-Infer**). Explicit and inferred type-argument calls are
elaborated to a monomorphic ordinary `Call` before evaluation and lowering.

```ultraviolet
let id: Handle = makeHandle<Texture>(raw_id)
```

#### 17.4.4 The capability / method call operator `~>`

The `~>` operator performs a **method or capability call** on a receiver:
`receiver~>name(args)`, parsed as `MethodCall(base, name, args)`. The receiver
is evaluated, the method `name` is resolved against the receiver's type
(including modal state methods, concrete class methods, capability methods, and
dynamic `$Class` receivers), arguments are checked against the method signature,
and the call has the method's return type.

The receiver's permission must admit the method's required receiver permission;
otherwise the call is rejected. An unknown method name (`E-SEM-2536`, method not
found) or an inaccessible method is also rejected. The argument pass-kind rules
from §17.4.2 apply to method arguments as well. The receiver itself may be a
`move` place, taken by value (`RecvArgMode(base) = move` when `base = move p`).

`~>` is a **postfix** operator (the highest precedence tier), so it chains left
to right and binds tighter than every binary operator:

```ultraviolet
// Each call's receiver is the result of the previous capability call.
let processed = source~>map(scale)~>filter(is_positive)~>take(16)

// Capability call on a narrowed capability.
let child_cap = parent_cap~>attenuate(reduced_scope)

// Method on a modal receiver.
let monotonic_ns: u64 = time~>monotonic()
```

Use `~>` for **all** method and capability invocations. Do not attempt to call a
method with `.name(args)`: `.name` is field access, and a following `(args)`
would call the field's value, not invoke a method.

### 17.5 Operator Expressions (§16.4)

This section is the exact reference for every unary and binary operator. Within
the §16.4 layer the prefix grammar is `unary_operator ::= "!" | "-"`; the
additional prefix forms `& * move` are owned by §16.8 (effectful core)
and §13.5 (`widen`), and are covered in §17.9 and the Types chapter.

#### 17.5.1 Unary operators `!` and `-`

- `!` on a `bool` is logical negation (**T-Not-Bool**): `!true ⇓ false`.
- `!` on an integer `t ∈ IntTypes` is the **bitwise complement** (one's
  complement) and preserves the type (**T-Not-Int**). `BitNot` complements all
  `IntWidth(t)` bits of the two's-complement representation. There is no separate
  `~` complement operator: integer bitwise NOT is `!`.
- `-` is arithmetic negation on signed integers and floats only
  (`t ∈ SignedIntTypes ∪ FloatTypes`, **T-Neg**). Negation of an **unsigned**
  integer is not defined. Signed integer negation panics on overflow (negating
  the minimum value, reason `Overflow`); floating negation is total, preserves
  width, and never panics.

```ultraviolet
let inverted: u8 = !0b0000_1111u8     // 0b1111_0000 = 0xF0
let flag: bool = !is_ready
let neg: i32 = -count                 // signed negation
```

#### 17.5.2 Arithmetic operators `+ - * / % **`

```text
ArithOps = { "+", "-", "*", "/", "%", "**" }
```

Both operands must have the same numeric type `t ∈ NumericTypes`
(`= IntTypes ∪ FloatTypes`); the result has that type (**T-Arith**). Arithmetic
does **not** implicitly widen or convert operands — mixing `i32` and `i64`
requires an explicit cast (§17.6).

Integer semantics (`ArithEval`, §16.4.5):

- `+ - *` compute the exact mathematical result; it MUST be in range or
  evaluation **panics with `Overflow`**.
- `/ %` require a non-zero divisor (`x2 ≠ 0`) or evaluation **panics with
  `DivZero`**; the result must also be in range (`Overflow` is also possible,
  e.g. `INT_MIN / -1`).
- `**` (power) requires a non-negative integer exponent (`x2 ≥ 0`) and computes
  `PowInt`; the result must be in range.

Float semantics: `+ - * /` follow IEEE 754-2019; `%` is the IEEE remainder; `**`
is the IEEE pow. Float arithmetic is **total** (never panics) — overflow yields
infinities and invalid operations yield NaN.

Lowering inserts panic checks exactly per `BinOpPanicReasons`:

```text
BinOpPanicReasons(op, T) =
  [Overflow]            if op ∈ {"+","-","*","**"} ∧ StripPerm(T) = TypePrim(t) ∧ t ∈ IntTypes
  [DivZero, Overflow]   if op ∈ {"/","%"}          ∧ StripPerm(T) = TypePrim(t) ∧ t ∈ IntTypes
  [Shift]               if op ∈ {"<<",">>"}
  []                    otherwise
```

```ultraviolet
let sum: u32   = a + b               // panics if it overflows u32
let mean: f32  = (x + y) / 2.0f32    // total: no panic, IEEE division
let square: i64 = base ** 2          // exponent must be >= 0
```

#### 17.5.3 Comparison operators `== != < <= > >=`

Equality `==`/`!=` applies to any `EqType`: the numeric types, `bool`, `char`,
safe pointers (`Ptr<T>`), raw pointers, `string`, `bytes`, and **unit enums**
(enums whose every variant is payload-free). Both operands must have the same
type; the result is `bool` (**T-Compare-Eq**).

Ordering `< <= > >=` applies to `OrdType`: integers, floats, and `char` only
(**T-Compare-Ord**). The result is `bool`.

Float comparison honors NaN (`Cmp`, §16.4.5): any **ordered** comparison with a
NaN operand yields `false`, `==` with NaN yields `false`, and `!=` with NaN
yields `true`. Comparison is left-chained by the grammar, but each comparison is
a separate `bool`-producing operation; comparisons do **not** fold into a single
relational chain — combine them with `&&`.

```ultraviolet
let in_range: bool = lo <= value && value < hi   // explicit && between comparisons
let equal_kind: bool = state == PlaybackState::Cooked  // unit-enum equality
```

#### 17.5.4 Logical operators `&& || !`

```text
LogicOps = { "&&", "||" }
```

`&&` and `||` require both operands to be `bool` and produce `bool`
(**T-Logical**). They **short-circuit**: `&&` evaluates the right operand only
when the left is `true` (**EvalSigma-Bin-And-True/False**); `||` evaluates the
right operand only when the left is `false` (**EvalSigma-Bin-Or-True/False**).
Unary logical negation is `!` on `bool` (§17.5.1).

```ultraviolet
let can_present: bool = has_focus && !should_reload
let needs_reload: bool = is_stale || force_reload
```

#### 17.5.5 Bitwise operators `& | ^` and complement `!`

```text
BitOps = { "&", "|", "^" }
```

The exact tokens are: bitwise **AND** `&`, bitwise **OR** `|`, bitwise **XOR**
`^`. Bitwise **complement** (NOT) is the unary `!` on integers (§17.5.1); there
is no `~` operator for complement in Ultraviolet.

Both operands of `& | ^` must be the **same integer type** `t ∈ IntTypes`; the
result has that type (**T-Bitwise**). The operation is defined bit-by-bit over
the `IntWidth(t)`-bit two's-complement representation (`BitOp`, §16.4.5): `&` =
AND, `|` = OR, `^` = XOR of corresponding bits. For a signed type the result is
reinterpreted back to signed (`ToSigned`); for unsigned it is the unsigned
value (`ToUnsigned`). Bitwise operators never panic.

Integer widths are fixed:

```text
IntWidth(i8)=8   IntWidth(i16)=16  IntWidth(i32)=32  IntWidth(i64)=64  IntWidth(i128)=128
IntWidth(u8)=8   IntWidth(u16)=16  IntWidth(u32)=32  IntWidth(u64)=64  IntWidth(u128)=128
IntWidth(isize)=8 × PointerSize    IntWidth(usize)=8 × PointerSize
```

Remember the precedence: `& | ^` all bind **looser** than comparison, so a mask
compared to a value must be parenthesized.

```ultraviolet
let masked: u32   = pixel & 0x00FF_FFFFu32      // clear the alpha byte
let combined: u8  = high_nibble | low_nibble    // OR two nibbles together
let toggled: u16  = flags ^ DIRTY_BIT           // flip a single flag bit
let complement: u8 = !mask                       // one's complement of mask

// Correct masking-and-test (parentheses required by precedence):
let is_set: bool = (flags & DIRTY_BIT) != 0u16
```

#### 17.5.6 Shift operators `<<` and `>>`

```text
ShiftOps = { "<<", ">>" }
```

A shift takes an integer left operand `t ∈ IntTypes` and a **`u32`** right
operand (the shift amount), and produces the left operand's type `t`
(**T-Shift**). The right operand MUST be `u32` — this is a hard typing rule, not
a convenience coercion. Supply the shift amount as a `u32` value or as an
unsuffixed literal in a `u32` position.

Semantics (`ShiftOp`, §16.4.5), where `w = IntWidth(t)` and `n` is the shift
amount:

- The amount must satisfy `0 ≤ n < w`. A shift amount `n ≥ w` leaves `ShiftOp`
  undefined and evaluation **panics with `Shift`**. Lowering always inserts a
  `Shift` check for `<<` and `>>`.
- `<<` is a logical left shift: the unsigned value is multiplied by `2^n` modulo
  `2^w`, then reinterpreted to the operand's signedness.
- `>>` computes `⌊x1 / 2^n⌋`, rounding **toward negative infinity**. It is
  therefore an **arithmetic (sign-extending) shift on signed operands** and a
  **logical shift on unsigned operands**.

Precedence: shifts bind tighter than `& | ^` but looser than `+ -`. So
`base + 1 << n` is `(base + 1) << n`, and `x << n & MASK` is `(x << n) & MASK`.

```ultraviolet
// Pack four bytes into a u32, big-endian. Shift amounts are u32.
procedure packBE(b0: u8, b1: u8, b2: u8, b3: u8) -> u32 {
    return (b0 as u32 << 24u32)
         | (b1 as u32 << 16u32)
         | (b2 as u32 << 8u32)
         | (b3 as u32)
}

// Extract bits [8, 16) of a u32 value.
procedure middleByte(word: u32) -> u8 {
    return ((word >> 8u32) & 0x00FFu32) as u8
}

// Logical vs arithmetic right shift depends on signedness of the left operand.
let logical: u16  = 0x8000u16 >> 1u32     // 0x4000 (zero-filled)
let arith:   i16  = (-32768i16) >> 1u32   // -16384 (sign-extended)
```

#### 17.5.7 Range expressions

```text
RangeKind = { To, ToInclusive, Full, From, Exclusive, Inclusive }
```

The six range forms from the grammar (§17.1) build range carrier values:
`a..b` (exclusive), `a..=b` (inclusive), `a..` (from), `..b` (to), `..=b`
(to-inclusive), and `..` (full). Ranges over `usize` are the index/slice ranges
(§17.3.4); discrete ranges over an integer/char type are iterable by `loop` (see
§17.8). A range expression evaluates its present bounds left to right and
constructs the corresponding `RangeVal` (**EvalSigma-Range**).

```ultraviolet
let window: [u8] = frame[0..header_len]   // exclusive usize range -> slice
loop i in 0..lane_count {                 // iterate 0, 1, ..., lane_count - 1
    process(i)
}
```

### 17.6 Cast and Transmute Expressions (§16.5)

#### 17.6.1 Syntax

```ebnf
cast_expr      ::= unary_expr ("as" type)?
transmute_expr ::= "transmute" "<" type "," type ">" "(" expression ")"
```

#### 17.6.2 `as` casts

A cast `e as T` is permitted exactly when `CastValid(S, T)` holds for the source
type `S` (**T-Cast**); otherwise it is rejected (**T-Cast-Invalid**,
`E-SEM-2528`). The valid cast pairs are:

- numeric → numeric (any of `IntTypes ∪ FloatTypes` to any other);
- `bool` → integer;
- integer → `bool`;
- `char` → `u32`;
- `u32` → `char`.

A cast where the source and target strip to the same type is the identity
(**CastVal-Id**). Conversion semantics (`CastVal`, §16.5.5):

- **Integer → integer.** The value is reduced modulo `2^w` of the target width
  `w` and reinterpreted: to a signed target via `ToSigned(w, x)`
  (**CastVal-Int-Int-Signed**), to an unsigned target via `ToUnsigned(w, x)`
  (**CastVal-Int-Int-Unsigned**). This is the canonical **widening / narrowing**
  rule: widening to a larger type preserves the value (sign-extending from a
  signed source, zero-extending from an unsigned source, by the modular
  definition); narrowing keeps only the low `w` bits. Integer-to-integer casts
  **never panic**.
- **Integer → float.** Rounds to the nearest representable float
  (**CastVal-Int-Float**); lowering preserves source signedness.
- **Float → float.** Rounds to the target format; NaN maps to the target's
  canonical NaN (**CastVal-Float-Float**).
- **Float → integer.** Truncates toward zero (`Trunc`); the truncated value MUST
  be in range for the target or the cast **panics with `Cast`**
  (**CastVal-Float-Int**; `CastNeedsCheck` is true).
- **bool → integer.** `false ⇒ 0`, `true ⇒ 1` (**CastVal-Bool-Int**).
- **integer → bool.** `0 ⇒ false`, non-zero ⇒ `true` (**CastVal-Int-Bool**).
- **char → u32.** The Unicode scalar code point (**CastVal-Char-U32**).
- **u32 → char.** Requires a valid Unicode scalar value or the cast **panics
  with `Cast`** (**CastVal-U32-Char**; `CastNeedsCheck` is true).

```ultraviolet
// Widening preserves value; narrowing keeps the low bits.
let wide: u32   = small_byte as u32          // zero-extend u8 -> u32
let narrow: u8  = (word & 0xFFu32) as u8     // keep low 8 bits
let signed: i32 = raw_u16 as i32             // u16 -> i32, value preserved

// Float/int conversions.
let pixels: i32 = (coverage * 255.0f32) as i32   // truncates; may panic if out of range
let ratio:  f32 = sample_count as f32

// char <-> u32 round trip.
let code: u32   = 'A' as u32                 // 65
let glyph: char = 0x41u32 as char            // 'A'; panics if not a scalar value
```

For decoding bytes into wider integers, the idiom is **cast each byte to the
wide type, then shift and OR** — the cast happens before the shift because `as`
binds tighter than `<<`:

```ultraviolet
let value: u32 = (byte0 as u32 << 8u32) | (byte1 as u32)
```

#### 17.6.3 `transmute`

`transmute<S, T>(e)` reinterprets the bits of an `S`-typed value as a `T`-typed
value. It is strictly an **unsafe** operation with three static requirements
(**T-Transmute**):

- it MUST appear inside an `unsafe` span, else it is rejected
  (**Transmute-Unsafe-Err**);
- the source and target sizes MUST be equal (`E-MEM-3031`);
- the source and target alignments MUST be equal (`E-UNS-0104`).

At run time it copies the source bits into the target representation
(`TransmuteVal`: equal bit patterns). If the target type admits invalid bit
patterns (`InvalidPatterns(T2)`), lowering inserts a runtime validity check and
the transmute **panics with `Cast`** on an invalid pattern; statically this also
emits the warning `W-SAF-0100`. `ValidTransmuteTarget` enumerates the
pattern-safe types: the scalar integer and float types, raw pointers, arrays of
valid targets, and `layout(C)` records whose fields are all valid targets.

Prefer `as` casts whenever a cast suffices; reach for `transmute` only at a
genuine representation boundary (for example, reading the raw IEEE-754 bits of a
float).

```ultraviolet
// Inspect the raw bit pattern of a float. Both types are 32-bit, 4-byte aligned.
procedure floatBits(x: f32) -> u32 {
    return unsafe { transmute<f32, u32>(x) }
}
```

### 17.7 Construction Expressions (§16.6)

#### 17.7.1 Syntax

```ebnf
tuple_literal       ::= "(" tuple_expr_elements? ")"
tuple_expr_elements ::= expression ";" | expression ("," expression)+
array_literal       ::= "[" array_segment_list? "]"
array_segment_list  ::= array_segment ("," array_segment)*
array_segment       ::= expression | expression ";" expression
record_literal      ::= identifier "{" field_init_list "}" | state_specific_type "{" field_init_list? "}"
field_init_list     ::= field_init ("," field_init)* ","?
field_init          ::= identifier ":" expression | identifier
enum_literal        ::= type_path "::" identifier variant_args?
variant_args        ::= "(" expression_list ")" | "{" field_init_list "}"
```

#### 17.7.2 Tuples

`()` is the unit value of type `()` (**T-Unit-Literal**). A one-element tuple
requires the trailing-semicolon form `(e;)` to distinguish it from a
parenthesized expression. A tuple of `n ≥ 1` elements is `(e1, e2, …, en)` and
has type `(T1, …, Tn)` (**T-Tuple-Literal**). Elements evaluate left to right.

```ultraviolet
let nothing: () = ()
let single: (i32;) = (42;)
let pair: (i32, f32) = (16, 0.25f32)
```

#### 17.7.3 Arrays

An array literal is a list of **segments**: a single element `e`
(`ArrayElemSegment`), or a **repeat** segment `e; count` producing `count`
copies (`ArrayRepeatSegment`) — **(T-Array-Literal-Segments)**. A repeat value's
type must be `Bitcopy`, and `count` must be a constant integer (type `usize` or
an integer type). All segments must share one element type `T`; the array's
length is the sum of the segment lengths.

```ultraviolet
let header: [u8; 4] = [0xDE, 0xAD, 0xBE, 0xEF]
let zeros: [u8; 256] = [0u8; 256]            // repeat segment
let mixed: [i32; 5] = [1, 2, 3; 2, 4]        // [1, 2, 3, 3, 4]
```

#### 17.7.4 Records

A record literal `Record { field: value, ... }` constructs a record value.
Every field must be initialized exactly once: field names must be **distinct**
(**Record-FieldInit-Dup**), the set of initialized fields must equal the
record's full field set (**Record-FieldInit-Missing**), and each field must be
visible (**Record-Field-Unknown**, **Record-Field-NotVisible**) —
**(T-Record-Literal)**. A **field shorthand** `field` is sugar for
`field: field` (a local of the same name). A field whose type is not `Bitcopy`
must be supplied with an explicit `move` from a place
(**Record-Field-NonBitcopy-Move**). Field initializers evaluate left to right.

```ultraviolet
let origin = Vec2 { x: 0.0f32, y: 0.0f32 }

procedure makeReply(frame_id: u64, skipped: bool) -> FrameReply {
    return FrameReply { frame_id, skipped }   // shorthand for frame_id: frame_id, ...
}
```

#### 17.7.5 Enums

After name resolution, enum constructors arise from qualified forms. The surface
syntax is `enum_literal ::= type_path "::" identifier variant_args?`. A **unit**
variant is `Enum::Variant`; a **tuple-payload** variant is `Enum::Variant(args)`;
a **record-payload** variant is `Enum::Variant { ... }`
(**T-Enum-Lit-Unit**, **T-Enum-Lit-Tuple**, **T-Enum-Lit-Record**). Variant
construction always uses `::`, never `.`.

```ultraviolet
let mode = PlaybackState::Cooked                       // unit variant
let event = InputEvent::Key(0x1Bu32)                   // tuple-payload variant
let win   = WindowMode::Windowed { width: 1280u32, height: 720u32 }
```

#### 17.7.6 Modal state construction

A `state_specific_type` literal `Modal@State { ... }` constructs a modal value
in a specific state (`RecordExpr(ModalStateRef(modal_ref, state), fields)`),
with the same field-completeness rules as records. The modal type name may carry
generic arguments, e.g. `Slot<Config>@Full { value: parsed }` for a generic modal `Slot<T>`. See the
Types chapter for modal type declarations and transitions.

```ultraviolet
let full: Slot<i32>@Full = Slot<i32>@Full { value: 7 }
```

### 17.8 Control Expressions (§16.7)

Control constructs are **expressions**: `if`, `loop`, and `{ ... }` blocks all
produce values.

#### 17.8.1 Syntax

```ebnf
if_expr        ::= "if" expression if_tail
if_tail        ::= block_expr ("else" (block_expr | if_expr))?
                 | "is" if_case_pattern block_expr ("else" (block_expr | if_expr))?
                 | "is" "{" if_case+ if_case_else? "}"
if_case        ::= if_case_pattern block_expr
if_case_pattern ::= pattern | ":" type
if_case_else   ::= "else" block_expr
loop_expr      ::= "loop" loop_condition? loop_invariant? block_expr
loop_condition ::= expression | pattern (":" type)? "in" expression
loop_invariant ::= "|:" "{" predicate_expr "}"
block_expr     ::= "{" statement* expression? "}"
```

#### 17.8.2 `if` as an expression

`if cond { ... } else { ... }` requires `cond : bool`; both branches must have
the **same** type `T`, and the whole `if` has type `T` (**T-If**). An `if`
**without** an `else` has type `()`, and its `then` branch must therefore have
type `()` (**T-If-No-Else**). `else` may be followed by a block or another `if`
(chained `else if`).

```ultraviolet
let clamped: i32 = if value < lo { lo } else if value > hi { hi } else { value }

if should_skip {
    return FrameReply::Skip        // value-less if: type ()
}
```

#### 17.8.3 `if ... is` — pattern and match expression form

`if scrutinee is pattern { ... }` tests the scrutinee against a single pattern,
binding any pattern variables in the `then` branch and narrowing the scrutinee
(**T-If-Is**). With an `else`, both branches must have the same type; without an
`else`, the form has type `()` (**T-If-Is-No-Else**). In a case position the
shorthand `: T` is a **type-test** pattern that elaborates to the typed discard
pattern `TypedPattern("_", T)` (i.e. `_: T`) before semantic analysis.

The **match-expression** form is `if scrutinee is { case+ else? }`
(`IfCaseExpr`): a list of case clauses, each `pattern block`, optionally
followed by `else block`. Every case body must produce the same type, and the
case set must be **exhaustive** or include the `else` fallback. Cases are tried
top to bottom and the first matching case wins. Specialized typing covers enum,
modal, and union scrutinees (`T-IfCase-Enum`, `T-IfCase-Modal`,
`T-IfCase-Union`; the general form is `T-IfCase-Other`).

```ultraviolet
// Single-pattern test with binding.
if event is InputEvent::Key(code) {
    handleKey(code)
}

// Match-expression form yielding a value.
let label: string = if state is {
    PlaybackState::Cooked   { "cooked" }
    PlaybackState::Raw      { "raw" }
    else                    { "unknown" }
}

// Type-test case shorthand `: T` (elaborates to `_: T`).
let width: u32 = if shape is {
    : Rect    { shapeRectWidth() }
    : Circle  { shapeCircleWidth() }
}
```

#### 17.8.4 `loop` as an expression

The three loop forms (`loop_condition` present or absent):

- **Infinite** `loop { ... }` (`LoopInfinite`): runs until a `break`. Its type
  is the join of all `break` values; with no value-bearing `break` and no plain
  `break`, its type is `!` (never), and with plain `break` it is `()`
  (`LoopTypeInf`, **T-Loop-Infinite**).
- **Conditional** `loop cond { ... }` (`LoopConditional`): runs while
  `cond : bool` is `true`. Its type is `()` unless `break value` supplies a value
  (`LoopTypeFin`, **T-Loop-Conditional**).
- **Iterating** `loop pattern in iterable { ... }` (`LoopIter`): binds each
  element to the pattern (**T-Loop-Iter**). The iterable must be a slice, array,
  or a range carrier (`LoopIterableType` admits `TypeSlice`, `TypeArray`,
  `TypeRange`, `TypeRangeInclusive`, `TypeRangeFrom`). Range-loop element types
  must implement `Discrete`, and bounded ranges (`a..b`, `a..=b`) must implement
  `Eq`.

A loop value is produced by `break value`. An optional loop invariant
`|: { predicate }` may precede the body.

```ultraviolet
// Iterating loop over a usize range.
var total: u32 = 0u32
loop i in 0..lane_count {
    total = total + sample(i)
}

// Conditional loop.
var remaining: usize = length
loop remaining > 0usize {
    remaining = remaining - 1usize
}

// Infinite loop producing a value via break.
let found: i32 = loop {
    let next = pollEvent()
    if next is InputEvent::Key(code) {
        break code as i32
    }
}
```

#### 17.8.5 Block expressions

A block `{ statement* expression? }` runs its statements and, if it ends with a
**tail expression** (no terminator), evaluates to that expression's value;
otherwise it evaluates to `()`. Block typing and statement sequencing are owned
by the Statements chapter (§18.1), but the value-producing behavior is what makes
blocks usable as expressions (for example, as `if`/`loop` branch bodies and
closure bodies).

```ultraviolet
let area: f32 = {
    let w = bounds.width
    let h = bounds.height
    w * h                         // tail expression: block value
}
```

### 17.9 Effectful Core Expressions (§16.8)

#### 17.9.1 Syntax

```ebnf
unsafe_expr     ::= "unsafe" block_expr
address_of_expr ::= "&" place_expr
move_expr       ::= "move" place_expr
copy_expr       ::= "copy" unary_expr
deref_expr      ::= "*" unary_expr
new_expr        ::= "new" unary_expr
propagate_expr  ::= postfix_expr "?"
```

Region allocation is either `new value` for the current scoped region or the
modal method call `receiver~>alloc(value)` on a `unique Region@Active` receiver
for an explicit target. `new` is contextual allocation syntax, not general
construction.

#### 17.9.2 `unsafe` blocks

`unsafe { ... }` is an expression (`UnsafeBlockExpr`) whose type and value are
those of its block (**T-Unsafe-Expr**). It marks its span as an `unsafe`
context, enabling operations that require it: raw-pointer dereference,
`transmute`, packed-field address-of, and `extern` calls. Keep `unsafe` blocks
as small as possible.

```ultraviolet
let bits: u32 = unsafe { transmute<f32, u32>(x) }
```

#### 17.9.3 Address-of `&` and dereference `*`

`&place` forms a **safe pointer** `Ptr<T>@Valid` to a place (**T-AddrOf**,
producing `TypePtr(T, Valid)`). The place must be addressable (`AddrOfOk`): an
index operand must be `usize`, and a `layout(packed)` record field requires
`unsafe`.

`*e` dereferences (`Deref`). A safe pointer `Ptr<T>@Valid` dereferences to `T`
(**T-Deref-Ptr**) and is a place. A **raw** pointer `*imm T` / `*mut T`
dereferences to `T` only inside `unsafe` (**T-Deref-Raw**). The dereferenced
type must be `Bitcopy` to be read by value.

```ultraviolet
let slot: Ptr<i32> = &frame_index
let current: i32 = *slot
```

#### 17.9.4 `move` and `copy`

`move place` takes ownership of the value in a place, leaving the place
moved-out (**T-Move**, `MoveExpr`). `copy e` duplicates a `Bitcopy` value
(**T-Copy**, `CopyExpr`); `copy` on a non-`Bitcopy` value is rejected
(`E-UNS-0107`, **ValueUse-NonBitcopyPlace**). At call sites, `move`/`copy` are
the argument pass-kinds of §17.4.2; as standalone expressions they produce the
moved or copied value. The operand of `move` is a `place_expr`; the operand of
`copy` is a `unary_expr`.

```ultraviolet
let owned_buffer = move scratch        // scratch is moved out
let independent = copy small_value     // duplicate a Bitcopy value
```

#### 17.9.5 Region allocation

`new value` allocates `value` in the innermost active scoped region.
`receiver~>alloc(value)` allocates `value` in the region named by `receiver`.
The receiver must have type `unique Region@Active`; both forms have the same
value type as `value` and carry region provenance. See the Memory/Regions
chapter for region scoping.

```ultraviolet
region {
    let node = new Node { value: 7, next: Ptr::null() }
}
```

#### 17.9.6 Error propagation `?`

`e?` propagates an error result (`Propagate(e)`). The behavior depends on the
shape of `e` and the enclosing procedure's signature:

- For an `Outcome`-typed `e` in a non-async procedure, the success variant
  (`Outcome::Value`) produces the contained value, and the error variant (`Outcome::Error`)
  returns early with the error re-wrapped to the procedure's outcome type
  (**T-Propagate-Outcome**, **EvalSigma-Propagate-Success/Error-Outcome**). The
  source error type must be a subtype of the procedure's error type.
- For a union-typed `e`, the success member is produced and a non-success member
  triggers an early return (**T-Propagate**, the `-Union` evaluation rules).
- In an **async** procedure with a non-`!` error type, `?` integrates with async
  failure (**T-Async-Try-Outcome**, **T-Async-Try**). Using `?` in an async
  procedure whose error type is `!` is rejected
  (**Async-Try-Infallible-Err**).

A successful payload of an `Outcome<T, E>` procedure is constructed with
`Outcome::Value(v)` and the error path with `Outcome::Error(e)` (or introduced
implicitly by `return`ing the payload in an `Outcome`-typed context).

```ultraviolet
procedure loadConfig(path: string) -> Outcome<Config, IoError> {
    let raw: bytes = readFile(path)?      // returns early on IoError
    let parsed: Config = parseConfig(raw)?
    return Outcome::Value(parsed)
}
```

### 17.10 Closure and Pipeline Expressions (§16.9)

#### 17.10.1 Syntax

```ebnf
pipeline_expr      ::= postfix_expr ("=>" postfix_expr)*
closure_expr       ::= "|" closure_param_list? "|" ("->" type)? closure_body
closure_param_list ::= closure_param ("," closure_param)* ","?
closure_param      ::= "move"? identifier (":" type)?
closure_body       ::= expression | block_expr
```

A typed closure parameter annotation whose outermost type constructor is a
`union_type` MUST be parenthesized as `("(" type ")")`.

#### 17.10.2 Closures

A closure `|params| body` (optionally `|params| -> Ret body`) is an anonymous
function (`ClosureExpr`). Its body is a single expression or a block. Each
parameter may be annotated `name: T`; if a parameter is unannotated, the closure
must be checked against an expected function/closure type so its parameter types
can be inferred, otherwise inference fails (`E-SEM-2591`,
**Infer-Closure-Params-Err**). A parameter annotation whose top-level type is a
**union** must be parenthesized: `|x: (A | B)| ...`.

Capture (§16.9.4):

- A **non-capturing** closure (empty `CaptureSet`) has a plain function type
  `TypeFunc(params, R)` (**T-Closure-NonCapturing**).
- A **capturing** closure has a closure type `TypeClosure(params, R, deps)`
  (**T-Closure-Capturing** for a local closure, **T-Closure-Escaping** for an
  escaping closure). `const` and `shared` captures are taken **by reference**; a
  parameter or place marked `move` is moved into the environment. Capturing a
  `unique` binding is rejected (**Capture-Unique-Err**).

A closure is **invoked with ordinary call syntax** `c(args)` (**T-ClosureCall**).

```ultraviolet
let scale = |s: i16| -> i16 { s * 2i16 }
let doubled: i16 = scale(21i16)

let bias: i32 = 10
let add_bias = |x: i32| x + bias        // captures bias by reference
let shifted: i32 = add_bias(5)
```

#### 17.10.3 Pipelines `=>`

The pipeline operator `=>` feeds a value into a single-argument function or
closure. It evaluates the left operand before the right callable, then applies
the callable to that value (§16.9.5, `PipelineExpr`). The right operand must
be a function or closure (`TypeFunc` / `TypeClosure`) taking **exactly one**
parameter, whose parameter type the left operand satisfies (`<:`); the pipeline
has the callee's return type (**T-Pipeline**). Violations are diagnosed:
non-callable RHS (`E-SEM-2538`, **T-Pipeline-NotCallable-Err**); type mismatch
(`E-SEM-2539`, **T-Pipeline-TypeMismatch-Err**); wrong arity (`E-SEM-2539`,
**T-Pipeline-ArgCount-Err**). Pipelines are left-associative, so `a => f => g`
evaluates as two left-first stages: first `a => f`, then that result `=> g`.

```ultraviolet
// Left-associative pipeline stages: raw_input, then normalize, then clamp.
let result: i32 = raw_input => normalize => clamp

// Same result when the calls are pure and ordinary call sequencing is irrelevant.
let same: i32 = clamp(normalize(raw_input))
```

### Idioms & Best Practices

- **Parenthesize bitwise-and-compare.** Because `& | ^` bind looser than
  comparison, always write `(x & MASK) == 0`, never `x & MASK == 0`. The same
  applies to shifts mixed with arithmetic: prefer `(base + offset) << shift`
  with explicit parentheses even when precedence already gives you what you
  want — the style guide values legibility during review over terseness.
- **Shift amounts are `u32`.** The right operand of `<<`/`>>` must be `u32` by
  rule. Write shift amounts as `u32` values or unsuffixed literals in `u32`
  position (`x >> 8u32`), not as `usize` or `i32`.
- **Cast before shift when decoding.** `as` binds tighter than `<<`, so
  `byte as u32 << 24u32` widens first, then shifts — exactly what a byte decoder
  wants. Widen narrow bytes to the accumulator type *before* combining them so
  the shift does not overflow the narrow type.
- **Prefer `as` over `transmute`.** Use numeric `as` casts for value
  conversions. Reserve `transmute` for genuine representation reinterpretation,
  keep it inside a minimal `unsafe` block, and confirm equal size and alignment.
- **Use `~>` for every method and capability call.** A `.name` is field access,
  not a method call. Method and capability invocation is always
  `receiver~>name(args)`.
- **Use `::` for variants, `@State{ ... }` for modal construction.** Enum
  variants are `Enum::Variant`; modal values are `Modal@State{ ... }`. Never
  construct a variant with a `.`.
- **Encode state in the result type, not booleans.** Use `if ... is { ... }`
  match expressions over enums and modal states rather than chains of boolean
  comparisons; the exhaustiveness check then guarantees you handle every case.
- **Keep authority narrow at call sites.** Pass only the capabilities a
  procedure needs, and use `move`/`copy` pass-kinds explicitly so ownership
  transfer is visible in the source.
- **Let `if`/`loop`/blocks produce values.** Prefer
  `let x = if cond { a } else { b }` and `let x = { ...; tail }` over mutable
  scaffolding when a single value is being computed.

### Pitfalls & Diagnostics

- **`x & MASK == 0` is misparsed**, becoming `x & (MASK == 0)` — an
  operator-operand mismatch (`E-SEM-2525`). Parenthesize the mask test.
- **Shift overflow panics.** A shift amount `n ≥ IntWidth(t)` is undefined and
  panics with reason `Shift` at run time; lowering always inserts the check.
  Mask or validate shift counts that come from data.
- **Integer arithmetic overflow panics.** `+ - * / % **` on integers panic on
  overflow (`Overflow`), and `/ %` panic on a zero divisor (`DivZero`). These
  are run-time panics, not silent wraps. If you need wrapping, do it explicitly
  with masking/casts. Float arithmetic, by contrast, is total and never panics.
- **No implicit numeric conversion.** Both operands of an arithmetic or bitwise
  operator must already be the *same* type; mixing widths is `E-SEM-2525`.
  Insert an explicit `as` cast.
- **Ordering operators are narrow.** `< <= > >=` apply only to integers, floats,
  and `char`. Other `EqType` values (pointers, strings, `bytes`, unit enums) are
  limited to `==`/`!=`.
- **Unsigned negation and non-`Bitcopy` copy are errors.** Unary `-` is defined
  only for signed integers and floats; `copy e` requires `e` to be `Bitcopy`
  (`E-UNS-0107`).
- **`transmute` has hard preconditions.** Outside `unsafe` it is rejected
  (`Transmute-Unsafe-Err`); mismatched sizes (`E-MEM-3031`) or alignments
  (`E-UNS-0104`) are rejected; a target admitting invalid bit patterns warns
  (`W-SAF-0100`) and inserts a run-time validity check that panics with `Cast`.
- **Float→int and u32→char casts can panic.** Out-of-range float truncation and
  non-scalar `u32 → char` casts panic with reason `Cast`. Range-check before
  casting untrusted values.
- **Call pass-kind errors.** A consuming (`move`) parameter without `move`/`copy`
  on the argument is `E-SEM-2534`; a stray `move` on a non-consuming parameter
  is `E-SEM-2535`; wrong argument count is `E-SEM-2532`; wrong type is
  `E-SEM-2533`; a non-callable callee is `E-SEM-2531`. A by-reference argument
  must be a place (`Call-Arg-NotPlace`).
- **Direct field access on a union is rejected** (`Union-DirectAccess-Err`).
  Narrow the union with `if ... is` before reading fields.
- **Fixed-array indices must be constant** outside a `#dynamic` scope
  (`E-UNS-0102`); a known out-of-bounds constant index is `E-UNS-0103`. Index a
  slice (or use a `#dynamic` scope) when the index is computed at run time.
- **Indexing a non-indexable type** is `E-SEM-2527`.
- **Pipeline targets must take one argument.** A non-callable RHS is
  `E-SEM-2538`; a type or arity mismatch is `E-SEM-2539`. The `=>` target must
  be a one-parameter function or closure.
- **Closure parameter inference can fail.** An unannotated closure parameter
  with no expected type is `E-SEM-2591`. Annotate the parameter or supply an
  expected closure type. Capturing a `unique` binding is rejected
  (`Capture-Unique-Err`).
