## 18. Patterns & Matching

Patterns are the means by which Ultraviolet *deconstructs* a value: they test a value's shape, bind names to its parts, and — in case analysis — drive the compiler's exhaustiveness and reachability guarantees. A pattern never evaluates; it either *matches* a value (producing a set of name bindings) or it does not. This chapter specifies every pattern form (spec §17.1–§17.4), the `if ... is` case-clause construct that consumes them (§17.5), and the exhaustiveness and reachability checking that makes case analysis safe (§17.6).

Patterns appear in three places in the language:

- **Irrefutable binding contexts** — `let`/`var` bindings and `loop pattern in expression` iteration variables, where the pattern *must* match every possible value. Refutable patterns are rejected in these positions (see *Pitfalls & Diagnostics*).
- **`if ... is` single-case form** — `if scrutinee is pattern { ... }`, optionally `else { ... }`.
- **`if ... is { ... }` case-list form** — the full match construct: a brace-delimited list of `pattern block` case clauses with an optional trailing `else`.

The pattern grammar is *identical* in every position; what differs is which patterns are *accepted* (irrefutable contexts reject refutable patterns) and how matching drives type narrowing and exhaustiveness.

This chapter cross-references **Chapter 8 (Data Types)** for records, enums, and tuples, **Chapter 9 (Modal Types)** for modal state, and **Chapter 17 (Expressions & Operators)** for the surrounding `if`/`loop` expression grammar. The `Outcome<TValue, TError>` enum used throughout for fallible results has exactly two variants, `Value(TValue)` and `Error(TError)`.

The complete pattern AST is a closed set:

```text
Pattern = { LiteralPattern(lit), WildcardPattern, IdentifierPattern(name),
            TypedPattern(name, type), TuplePattern(elems),
            RecordPattern(type_path, fields), EnumPattern(type_path, name, payload_opt),
            ModalPattern(state_name, fields_opt), RangePattern(kind, lo, hi) }
```

A central operation, **pattern name extraction** (`PatNames`), collects every identifier a pattern binds. It underpins the duplicate-binding check: a single pattern MUST NOT bind the same name twice (`Distinct(PatNames(pat))` must hold).

> **A note on the examples.** Ultraviolet has no string concatenation operator and no string interpolation: the arithmetic operators (`+`, `-`, `*`, `/`, `%`, `**`) are defined only when *both* operands are numeric primitives (`(T-Arith)`), and `string` exposes only a fixed set of built-in operations — there is no `string::concat`. The worked examples therefore never combine a string with a non-string value; each arm yields a string literal, an already-`string` binding, or a numeric/boolean value, matching the idioms used elsewhere in this handbook.

---

### 18.1 Basic Patterns

Basic patterns are the atoms of the pattern grammar: literals, the wildcard `_`, plain identifier bindings, and typed patterns.

#### 18.1.1 Syntax

```ebnf
basic_pattern ::= literal | "_" | identifier | typed_pattern
typed_pattern ::= ("_" | identifier) ":" type
```

A `literal` here is any token whose kind is `IntLiteral`, `FloatLiteral`, `StringLiteral`, `CharLiteral`, `BoolLiteral`, or `NullLiteral`.

#### 18.1.2 The four basic forms

**Literal pattern.** A literal pattern matches a value exactly equal to the literal. The literal's type is computed by `PatType` and must be a subtype of the scrutinee type (`(Pat-Literal-R)`). Untyped integer literals default to `i32`; unsuffixed or `f`-suffixed float literals default to `f32`; `bool`, `char`, and string literals take their primitive/string types. A literal pattern binds nothing.

```text
(Pat-Literal-R)
Γ ⊢ Literal(lit) : T_l    Γ ⊢ T_l <: T
────────────────────────────────────────
Γ ⊢ LiteralPattern(lit) ◁ T ⊣ ∅
```

**Wildcard pattern `_`.** The wildcard matches any value and binds nothing. It is the canonical "I don't care" pattern and is *irrefutable* (it always matches).

```text
(Pat-Wildcard-R)
──────────────────
Γ ⊢ _ ◁ T ⊣ ∅
```

**Identifier pattern.** A bare identifier matches any value and binds that value to the name. It is irrefutable. The bound name has the scrutinee's type `T`.

```text
(Pat-Ident-R)
──────────────────────
Γ ⊢ x ◁ T ⊣ {x ↦ T}
```

**Typed pattern.** A typed pattern `name : T_a` (or `_ : T_a`) is a *type test*: it matches when the runtime value is a member of `T_a` (`RuntimeTypeMember`). The annotated type must be *exactly equal* (≡) to the permission-stripped scrutinee type, or — when the scrutinee is a union — *exactly* one of the union's member types. A non-discard name binds the value at the narrowed type `T_a`; the discard form `_ : T_a` binds nothing.

```text
(Pat-Typed-Exact-R)
Γ ⊢ T_a type    Γ ⊢ T_a ≡ StripPerm(T)    B = ({x ↦ T_a} if x ≠ "_" else ∅)
────────────────────────────────────────────────────────────────────────────
Γ ⊢ TypedPattern(x, T_a) ◁ T ⊣ B

(Pat-Typed-Union-R)
Γ ⊢ T_a type    StripPerm(T) = TypeUnion([T_1, …, T_n])
∃ i. Γ ⊢ T_a ≡ StripPerm(T_i)    B = ({x ↦ T_a} if x ≠ "_" else ∅)
────────────────────────────────────────────────────────────────────
Γ ⊢ TypedPattern(x, T_a) ◁ T ⊣ B
```

Within an `if ... is` case position, the shorthand `: T` elaborates to the discard typed pattern `_ : T` (see §18.5). Outside `if_case_pattern`, `: T` is not standalone pattern syntax — you must write `_ : T` or `name : T`.

#### 18.1.3 Permission threading

When the scrutinee type carries a permission wrapper `TypePerm(p, U)`, matching strips the permission to check the inner pattern, then re-wraps every binding with the same permission via `PermWrap`. A value matched out of a `unique` place therefore binds at a `unique`-qualified type, preserving the permission discipline of **Chapter 20 (Permissions)**:

```text
(Pat-StripPerm)
Γ ⊢ pat ◁ StripPerm(T) ⊣ B
──────────────────────────────
Γ ⊢ pat ◁ T ⊣ PermWrap(T, B)
```

#### 18.1.4 Worked example

```ultraviolet
/// Classify a status code carried as a plain i32.
public procedure classify(code: i32) -> string {
    return if code is {
        0 {
            "unset"
        }
        200 {
            "ok"
        }
        other {
            "other"
        }
    }
}
```

The first two arms are literal patterns (refutable). The final arm `other` is an identifier pattern: irrefutable, binding the matched value to `other`, and therefore making the whole construct exhaustive over `i32` without an `else` (see §18.6.5). Each arm's value is the trailing expression of its block.

---

### 18.2 Tuple and Record Patterns

Tuple and record patterns deconstruct aggregate values positionally and by field name, respectively.

#### 18.2.1 Syntax

```ebnf
tuple_pattern          ::= "(" tuple_pattern_elements? ")"
tuple_pattern_elements ::= pattern ";" | pattern ("," pattern)+ ","?
record_pattern         ::= type_path "{" field_pattern_list? "}"
field_pattern_list     ::= field_pattern ("," field_pattern)* ","?
field_pattern          ::= identifier (":" pattern)?
```

The single-element tuple pattern is written with a trailing semicolon — `(p;)` — exactly as the single-element tuple *value* uses `;`. This is the *only* valid spelling of a one-element tuple pattern; a comma is not accepted for the single-element form. The empty tuple pattern `()` matches the unit value of type `()` (`(Pat-Tuple-Unit-R)`).

#### 18.2.2 Tuple patterns

A tuple pattern matches a tuple of the same arity, matching each sub-pattern against the corresponding element type and unioning the resulting bindings. An arity mismatch is a compile-time error.

```text
(Pat-Tuple-R)
StripPerm(T) = TypeTuple([T_1, …, T_n])    ∀ i, Γ ⊢ p_i ◁ T_i ⊣ B_i    B = ⊎_i B_i
─────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ TuplePattern([p_1, …, p_n]) ◁ T ⊣ B

(Pat-Tuple-R-Arity-Err)
StripPerm(T) = TypeTuple([T_1, …, T_n])    ps = [p_1, …, p_m]    m ≠ n
─────────────────────────────────────────────────────────────────────
Γ ⊢ TuplePattern(ps) ◁ T ⇑ Code(Pat-Tuple-Arity-Err)
```

The binding-union operator `⊎` is defined only when the two binding sets have disjoint domains. Two sub-patterns of the same tuple therefore MUST NOT bind the same name (the duplicate-binding diagnostic, §18.8).

#### 18.2.3 Record patterns

A record pattern is written `TypePath { field_patterns }`. Each `field_pattern` is one of:

- **Shorthand** `name` — binds the field's value to the local name `name`. This is the field-pattern with no `: pattern` tail; the AST desugaring is exact: `PatOf(⟨f, ⊥, _⟩) = IdentifierPattern(f)`.
- **Explicit** `name : pattern` — matches the field's value against `pattern`: `PatOf(⟨f, p, _⟩) = p`.

Each named field must exist on the record (an unknown field raises `RecordPattern-UnknownField`) and must be visible at the use site (`FieldVisible`). You may list a subset of fields; unlisted fields are ignored. A trailing comma is permitted.

```text
(Pat-Record-R)
StripPerm(T) = TypePath(p)    RecordDecl(p) = R
∀ fp ∈ io, FieldType(R, FieldName(fp)) = T_f ∧ FieldVisible(m, R, FieldName(fp))
         ∧ Γ ⊢ PatOf(fp) ◁ T_f ⊣ B_f    B = ⊎_{fp ∈ io} B_f
────────────────────────────────────────────────────────────────────────────────
Γ ⊢ RecordPattern(p, io) ◁ T ⊣ B

(RecordPattern-UnknownField)
StripPerm(T) = TypePath(p)    RecordDecl(p) = R
∃ fp ∈ io. FieldName(fp) ∉ FieldNameSet(R)
─────────────────────────────────────────────
Γ ⊢ RecordPattern(p, io) ⇑ Code(RecordPattern-UnknownField)
```

Consequently `Point { x, y }` binds `x` and `y` directly (shorthand), whereas `Point { x: 0, y }` tests `x` against the literal `0` and binds `y`.

#### 18.2.4 Worked example

```ultraviolet
public record Point {
    public x: i32
    public y: i32
}

/// Describe a point's position relative to the axes.
public procedure describe(p: Point) -> string {
    return if p is {
        Point { x: 0, y: 0 } {
            "origin"
        }
        Point { x: 0, y } {
            "y-axis"
        }
        Point { x, y: 0 } {
            "x-axis"
        }
        Point { x, y } {
            "interior"
        }
    }
}
```

The final arm binds both fields with irrefutable sub-patterns, so it is irrefutable for `Point` and the match is exhaustive without an `else`. Tuple destructuring works identically, by position:

```ultraviolet
/// Swap the components of a pair.
public procedure swap(pair: (i32, string)) -> (string, i32) {
    let (number, label) = pair
    return (label, number)
}
```

Here `(number, label)` is an irrefutable tuple pattern used in an irrefutable `let` binding.

---

### 18.3 Enum and Modal Patterns

Enum patterns match a value of an `enum` type against a specific variant; modal patterns match a value of a `modal` type against a specific state. Both may bind the matched case's payload.

#### 18.3.1 Syntax

```ebnf
enum_pattern                  ::= type_path "::" identifier enum_payload_pattern?
enum_payload_pattern          ::= "(" enum_payload_pattern_elements? ")"
                                | "{" field_pattern_list? "}"
enum_payload_pattern_elements ::= pattern ("," pattern)* ","?
modal_pattern                 ::= "@" identifier ("{" field_pattern_list? "}")?
```

An enum pattern names its variant with the `::` path operator: `Color::Red`, `Shape::Circle(r)`, `Event::Click { x, y }`. A single-element *tuple* enum payload is written `(p)` — it MUST NOT use the tuple single-element marker `;`, which remains specific to `tuple_pattern`.

A modal pattern names a state with the leading `@` sigil: `@Idle`, `@Open { handle }`, `@Closed`. Modal payloads are always record-shaped (field patterns), never positional tuples; the empty form `@S` (no braces) matches the state and binds nothing.

> **Resolution of `A::B { ... }`.** Name resolution classifies `A::B { ... }` by the resolved declarations. If `A::B` resolves to a *record type path*, the construct is a `RecordPattern`. If `A` resolves to an *enum type* and `B` to a *record-payload variant*, it is an `EnumPattern` with record payload. If both readings are simultaneously available in the same namespace relation, resolution emits `E-MOD-1307` (ambiguous).

#### 18.3.2 Enum pattern semantics

There are three variant shapes, each with its own typing rule. The variant must belong to the scrutinee's enum, and the payload pattern shape must agree with the variant's declared payload.

```text
(Pat-Enum-Unit-R)            -- C::Variant with no payload
StripPerm(T) = TypePath(p)   EnumDecl(p) = E   VariantPayload(E, v) = ⊥
──────────────────────────────────────────────────────────────────────
Γ ⊢ EnumPattern(p, v, ⊥) ◁ T ⊣ ∅

(Pat-Enum-Tuple-R)           -- C::Variant(p_1, …, p_n)
StripPerm(T) = TypePath(p)   EnumDecl(p) = E
VariantPayload(E, v) = TuplePayload([T_1, …, T_n])
∀ i, Γ ⊢ p_i ◁ T_i ⊣ B_i    B = ⊎_i B_i
─────────────────────────────────────────────────────────────────────
Γ ⊢ EnumPattern(p, v, TuplePayloadPattern([p_1, …, p_n])) ◁ T ⊣ B

(Pat-Enum-Record-R)          -- C::Variant { field: p, … }
StripPerm(T) = TypePath(p)   EnumDecl(p) = E
VariantPayload(E, v) = RecordPayload(io')
∀ fp ∈ io, EnumFieldType(E, v, FieldName(fp)) = T_f ∧ Γ ⊢ PatOf(fp) ◁ T_f ⊣ B_f
B = ⊎_{fp ∈ io} B_f
─────────────────────────────────────────────────────────────────────
Γ ⊢ EnumPattern(p, v, RecordPayloadPattern(io)) ◁ T ⊣ B
```

#### 18.3.3 Modal pattern semantics

A modal pattern `@S { ... }` matches a modal value currently in state `S`, binding any listed payload fields. It applies whether the scrutinee is typed as a general modal reference (`ModalRefType`) — in which case the modal value carries a runtime state tag — or as a specific modal-state type (`TypeModalState(modal_ref, S)`):

```text
(Pat-Modal-R)            -- scrutinee is a general modal reference
StripPerm(T) = ModalRefType(modal_ref)   ModalDeclOf(modal_ref) = M   S ∈ States(M)
∀ fp ∈ io, ModalPayloadMap(modal_ref, S)(FieldName(fp)) = T_f ∧ Γ ⊢ PatOf(fp) ◁ T_f ⊣ B_f
B = ⊎_{fp ∈ io} B_f
──────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ModalPattern(S, io) ◁ T ⊣ B

(Pat-Modal-State-R)      -- scrutinee already pinned to a specific state
StripPerm(T) = TypeModalState(modal_ref, S)   ModalDeclOf(modal_ref) = M
∀ fp ∈ io, ModalPayloadMap(modal_ref, S)(FieldName(fp)) = T_f ∧ Γ ⊢ PatOf(fp) ◁ T_f ⊣ B_f
B = ⊎_{fp ∈ io} B_f
──────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ModalPattern(S, io) ◁ T ⊣ B
```

Matching a modal pattern *narrows* the scrutinee: inside the arm, the scrutinee binding (when it is a plain identifier) is refined to `TypeModalState(modal_ref, S)`, so state-specific fields, methods, and transitions become available. This is governed by `PatternNarrow` (§18.5) and is the chief reason `if ... is` is the idiomatic way to drive a modal protocol.

The dynamic rules confirm the runtime behavior: a modal value is a tagged pair `⟨S, v⟩`; an empty modal pattern `@S` matches the tag with no bindings, and `@S { io }` additionally matches the payload record:

```text
(Match-Modal-Empty)
─────────────────────────────────
Γ ⊢ MatchModal(@S, ⟨S, v⟩) ⇓ ∅

(Match-Modal-Record)
Γ ⊢ MatchRecord(io, v) ⇓ B
─────────────────────────────────
Γ ⊢ MatchModal(@S{io}, ⟨S, v⟩) ⇓ B
```

#### 18.3.4 Worked example — enums

```ultraviolet
public enum Shape {
    Point
    Circle(f32)
    Rect { width: f32, height: f32 }
}

/// Compute a shape's area.
public procedure area(shape: Shape) -> f32 {
    return if shape is {
        Shape::Point {
            0.0
        }
        Shape::Circle(radius) {
            3.14159265 * radius * radius
        }
        Shape::Rect { width, height } {
            width * height
        }
    }
}
```

`Shape::Point` is a unit-variant pattern; `Shape::Circle(radius)` binds the single tuple-payload element; `Shape::Rect { width, height }` binds both record-payload fields by shorthand. Every variant is covered with irrefutable payloads, so no `else` is needed (see §18.6). Note that enum variants are separated by statement terminators (newline or `;`), never commas.

#### 18.3.5 Worked example — modal state

```ultraviolet
public modal Connection {
    @Closed {
    }
    @Open {
        public endpoint: string
        public bytes_sent: u64
    }
    @Faulted {
        public reason: string
    }
}

/// Return the active endpoint, or the empty string when not open.
public procedure currentEndpoint(conn: Connection) -> string {
    return if conn is {
        @Closed {
            ""
        }
        @Open { endpoint, bytes_sent } {
            endpoint
        }
        @Faulted { reason } {
            reason
        }
    }
}
```

Because every state of `Connection` is covered with irrefutable payload sub-patterns, the construct is exhaustive (§18.6) and the compiler accepts it with no `else`. Inside `@Open { endpoint, bytes_sent }`, the bindings `endpoint` (a `string`) and `bytes_sent` (a `u64`) are the destructured payload; the arm yields the already-`string` binding `endpoint` directly.

#### 18.3.6 Worked example — Outcome

`Outcome<TValue, TError>` is the standard fallible-result enum. It has exactly two variants: `Value(TValue)` and `Error(TError)`. You match it with enum `::` patterns:

```ultraviolet
/// Return the loaded text on success, or a fixed fallback on failure.
public procedure readConfigOr(result: Outcome<string, IoError>, fallback: string) -> string {
    return if result is {
        Outcome::Value(value) {
            value
        }
        Outcome::Error(error) {
            fallback
        }
    }
}
```

Covering both `Outcome::Value` and `Outcome::Error` is exhaustive for `Outcome`, so the result of the `if ... is` expression is well-typed without an `else`. This is the canonical safe pattern for consuming fallible results: the type system *forces* you to handle the error case.

---

### 18.4 Range Patterns

A range pattern matches an integer that lies within a constant interval.

#### 18.4.1 Syntax

```ebnf
range_pattern ::= pattern (".." | "..=") pattern
```

`..` is an **exclusive** upper bound (`Exclusive`); `..=` is an **inclusive** upper bound (`Inclusive`). The bounds are themselves patterns, but for a range to typecheck both must be compile-time integer-literal constants. `ConstPatInt(p) = n` holds exactly when `p = LiteralPattern(IntLiteral(n))`.

#### 18.4.2 Semantics

The scrutinee must be an integer primitive (`t ∈ IntTypes`). Both bounds must be integer-literal constants, and the range must be non-empty: for `..`, the low bound must be strictly less than the high bound; for `..=`, the low bound must be less than or equal to the high bound. A range pattern binds nothing.

```text
(Pat-Range-R)
StripPerm(T) = TypePrim(t)    t ∈ IntTypes
ConstPatInt(p_l) = n_l    ConstPatInt(p_h) = n_h
(kind = ".." ⇒ n_l < n_h)    (kind = "..=" ⇒ n_l ≤ n_h)
───────────────────────────────────────────────────────
Γ ⊢ RangePattern(kind, p_l, p_h) ◁ T ⊣ ∅
```

At runtime, `p_l ..= p_h` matches `v` when `v_l ≤ v ≤ v_h`, and `p_l .. p_h` matches when `v_l ≤ v < v_h`.

Two compile-time errors guard range patterns:

```text
(RangePattern-NonConst)        -- a bound is not a compile-time constant
(ConstPatInt(p_l) undefined ∨ ConstPatInt(p_h) undefined)
─────────────────────────────────────────────────────────
Γ ⊢ RangePattern(kind, p_l, p_h) ⇑ Code(RangePattern-NonConst)

(RangePattern-Empty)           -- the interval contains no integers
ConstPatInt(p_l) = n_l    ConstPatInt(p_h) = n_h
((kind = "..") ⇒ n_l ≥ n_h)    ((kind = "..=") ⇒ n_l > n_h)
─────────────────────────────────────────────────────────────
Γ ⊢ RangePattern(kind, p_l, p_h) ⇑ Code(RangePattern-Empty)
```

#### 18.4.3 Worked example

```ultraviolet
/// Map an HTTP status code to a coarse category.
public procedure category(code: u16) -> string {
    return if code is {
        100 ..= 199 {
            "informational"
        }
        200 ..= 299 {
            "success"
        }
        300 ..= 399 {
            "redirect"
        }
        400 ..= 499 {
            "client-error"
        }
        500 ..= 599 {
            "server-error"
        }
        other {
            "unknown"
        }
    }
}
```

Range patterns are refutable, so the trailing identifier arm `other` is required to make the integer match exhaustive (§18.6.5). Note `..` versus `..=`: `0 .. 10` matches `0` through `9`; `0 ..= 10` matches `0` through `10`.

---

### 18.5 Case Clauses

Case clauses are consumed by the `if ... is` construct, which is the language's pattern-matching expression. The pattern grammar of §18.1–§18.4 is the same in every position; this section specifies how the case construct is parsed, scoped, type-checked, and evaluated.

#### 18.5.1 Syntax

The `if ... is` forms are part of the control-expression grammar (Chapter 17):

```ebnf
if_expr         ::= "if" expression if_tail
if_tail         ::= block_expr ("else" (block_expr | if_expr))?
                  | "is" if_case_pattern block_expr ("else" (block_expr | if_expr))?
                  | "is" "{" if_case+ if_case_else? "}"
if_case         ::= if_case_pattern block_expr
if_case_pattern ::= pattern | ":" type
if_case_else    ::= "else" block_expr
block_expr      ::= "{" statement* expression? "}"
```

There are two pattern-matching shapes:

- **Single-case** `if scrutinee is pattern { ... }` — match one pattern; an optional `else (block | if_expr)` runs on no match.
- **Case-list** `if scrutinee is { if_case+ if_case_else? }` — a brace-delimited list of *one or more* case clauses, each a pattern followed immediately by its block, with an optional trailing `else` block.

A case clause has no separator punctuation: it is `pattern block`, and the block's braces delimit it. The `: T` shorthand is valid *only* in `if_case_pattern` position and elaborates to `TypedPattern("_", T)` before semantic analysis; it does not add a distinct AST node.

Each case clause is the AST node `IfCase = ⟨pat, body⟩`.

#### 18.5.2 Guards via nested matching

Ultraviolet case clauses have no dedicated guard keyword. A guarded arm is expressed by binding in the pattern and refining inside the block: because each arm body is a full `block_expr`, arbitrary boolean conditions are expressible there with a nested `if`. Multi-condition dispatch that cannot be written as a single pattern is written as a pattern that binds, followed by an inner `if` that distinguishes the sub-cases:

```ultraviolet
/// Bucket a signed reading, distinguishing exact zero from sign.
public procedure bucket(reading: i32) -> string {
    return if reading is {
        0 {
            "zero"
        }
        n {
            if n > 0 {
                "positive"
            } else {
                "negative"
            }
        }
    }
}
```

The `0` literal arm acts as the precise case; the `n` identifier arm binds the remainder, and an inner `if` performs the guard.

#### 18.5.3 Scoping and type narrowing

Each case clause is type-checked in a scope that introduces the pattern's bindings and, crucially, *narrows* the scrutinee when the scrutinee is a plain identifier (`ScrutineeBinding(e) = x`). The narrowing relation `PatternNarrow` computes the refined type a successful match implies:

- For a modal pattern `@S{io}` over a general modal reference, the scrutinee narrows to `TypeModalState(modal_ref, S)` — the specific state — so state-specific members become usable in the arm (`PatternNarrow-ModalRef`).
- For a typed pattern `_ : T_a` / `x : T_a`, the scrutinee narrows to `T_a` (`PatternNarrow-Typed`).
- For a union scrutinee, narrowing distributes over the union members and keeps only those a pattern can match (`PatternNarrow-Union`).
- Permission wrappers are preserved across narrowing (`PatternNarrow-Perm`).

```text
(CaseScope-Narrow)
Γ ⊢ pat ◁ T_s ⊣ B    Distinct(PatNames(pat))    ScrutineeBinding(e) = x
PatternNarrow(Γ, pat, T_s) ⇓ T_n    RefineBinding(Γ, x, T_n) ⇓ Γ_r
Γ_0 = PushScope(Γ_r)    IntroAll(Γ_0, B) ⇓ Γ_case
────────────────────────────────────────────────────────────────────────
CaseScope(Γ, e, pat, T_s) ⇓ Γ_case
```

When the scrutinee is not a plain identifier, or the pattern does not narrow, the bindings are introduced into a fresh scope without refining the scrutinee (`CaseScope-PatternOnly`).

The dual relation `ElseScope` narrows the *negative* path: when a single-case pattern is rejected, the scrutinee in the `else` branch may be refined to the *complement* type (`PatternRejectNarrow`). For a union scrutinee, ruling out one member narrows the `else` branch to the remaining members. In the case-list form, `CasesElseScope` threads this rejection narrowing through every preceding clause so the trailing `else` sees the residual type. This makes single-case `if ... is ... else` a precise type-test-and-refine tool.

All case arms (and the `else`, if present) must produce a *common* result type `T_r`; the value of the whole `if ... is` expression is `T_r`. In checking mode each arm body is checked against the expected type `T` directly (`Chk-IfCase-*`).

#### 18.5.4 Evaluation semantics

Case clauses are tried **top to bottom**. The first clause whose pattern matches the scrutinee wins (`EvalIfCases-Head`); its bindings are introduced, its block evaluates, and that block's result is the value of the construct. A clause whose pattern does not match yields `NoMatch` and the next clause is tried (`EvalIfCases-Tail`). Remaining clauses are not tried once a clause hits.

```text
(EvalIfCase-Hit)
Γ ⊢ MatchPattern(pat, v) ⇓ B    BindOrder(pat, B) = binds
BlockEnter(σ, binds) ⇓ (σ_1, scope)    Γ ⊢ EvalBlockSigma(body, σ_1) ⇓ (out, σ_2)
BlockExit(σ_2, scope, out) ⇓ (out', σ_3)
─────────────────────────────────────────────────────────────────────────────────
Γ ⊢ EvalIfCaseSigma(⟨pat, body⟩, v, σ) ⇓ (Match(out'), σ_3)

(EvalIfCases-Tail)
Γ ⊢ EvalIfCaseSigma(c, v, σ) ⇓ (NoMatch, σ_1)
Γ ⊢ EvalIfCaseListSigma(cs, else_opt, v, σ_1) ⇓ (out, σ_2)
────────────────────────────────────────────────────────────
Γ ⊢ EvalIfCaseListSigma(c::cs, else_opt, v, σ) ⇓ (out, σ_2)
```

If no clause matches and an `else` is present, the `else` block runs:

```text
(EvalIfCases-Else)
else_opt = b    Γ ⊢ EvalBlockSigma(b, σ) ⇓ (out, σ')
────────────────────────────────────────────────────
Γ ⊢ EvalIfCaseListSigma([], else_opt, v, σ) ⇓ (out, σ')
```

If no clause matches and there is *no* `else`, evaluation panics:

```text
(EvalIfCases-None)
else_opt = ⊥
───────────────────────────────────────────────────────
Γ ⊢ EvalIfCaseListSigma([], else_opt, v, σ) ⇓ (Ctrl(Panic), σ)
```

This panic branch is only *reachable* for scrutinee categories the compiler cannot prove exhaustive; for enums, modals, and unions the exhaustiveness checker of §18.6 guarantees a covered case before runtime. The lowering rule confirms it: when `else_opt = ⊥`, `LowerIfCases` MUST emit a trailing arm that lowers to `LowerPanic(MatchFail)`, reached only when no clause matched.

#### 18.5.5 Worked example — exhaustive enum dispatch with narrowing

```ultraviolet
public enum Token {
    Eof
    Number(i64)
    Word(string)
}

/// Render a token's category for diagnostics.
public procedure category(token: Token) -> string {
    return if token is {
        Token::Eof {
            "eof"
        }
        Token::Number(value) {
            "number"
        }
        Token::Word(text) {
            text
        }
    }
}
```

Every variant of `Token` is covered, so the construct is exhaustive and produces a `string` with no `else`. The `Token::Word(text)` arm yields its bound `string` payload directly. Adding a new variant to `Token` later would make this match non-exhaustive and the compiler would reject it — see §18.6.

---

### 18.6 Exhaustiveness and Reachability

Exhaustiveness checking is what makes `if ... is { ... }` *safe*: for the structured scrutinee categories — enums, general modal types, and unions — a case construct without an `else` is accepted *only if* the clauses provably cover every possible value. Reachability checking rejects clauses that can never match because an earlier clause already subsumes them.

#### 18.6.1 Irrefutability and coverage

A pattern is **irrefutable** for type `T` when it cannot fail to match any value of `T`:

```text
Irrefutable(pat, T) ⇔
    pat = WildcardPattern
  ∨ pat = IdentifierPattern(_)
  ∨ (pat = TypedPattern(_, T_a) ∧ T_a = StripPerm(T))
  ∨ (pat = TuplePattern([p_1, …, p_n]) ∧ StripPerm(T) = TypeTuple([T_1, …, T_n])
       ∧ ∀ i. Irrefutable(p_i, T_i))
  ∨ (pat = RecordPattern(p, io) ∧ StripPerm(T) = TypePath(p) ∧ RecordDecl(p) = R
       ∧ ∀ fp ∈ io. Irrefutable(PatOf(fp), FieldType(R, FieldName(fp))))
```

A case construct is trivially exhaustive if it contains *any* irrefutable case for the scrutinee type (`HasIrrefutableCase`). Otherwise, coverage is decided per scrutinee category.

A variant, state, or union member counts toward coverage *only* when its case pattern matches the **whole** case with irrefutable payload sub-patterns. A clause whose payload sub-patterns are refutable matches only *part* of its variant/state/member and **MUST NOT** contribute to exhaustiveness:

```text
CoversVariant(EnumPattern(_, v, ⊥), E, v) ⇔ VariantPayload(E, v) = ⊥
CoversVariant(EnumPattern(_, v, TuplePayloadPattern([p_1, …, p_n])), E, v) ⇔
    VariantPayload(E, v) = TuplePayload([T_1, …, T_n]) ∧ ∀ i. Irrefutable(p_i, T_i)
CoversVariant(EnumPattern(_, v, RecordPayloadPattern(io)), E, v) ⇔
    VariantPayload(E, v) = RecordPayload(_)
    ∧ ∀ fp ∈ io. Irrefutable(PatOf(fp), EnumFieldType(E, v, FieldName(fp)))

CoversState(ModalPattern(S, io), modal_ref, S) ⇔
    ∀ fp ∈ io. Irrefutable(PatOf(fp), ModalPayloadMap(modal_ref, S)(FieldName(fp)))

CoversMember(p, T) ⇔ Irrefutable(p, T)
```

`CoversVariant` and `CoversState` hold for no other pattern form. The covered sets are then `CoveredVariants(cases, E)` and `CoveredStates(cases, modal_ref)`.

#### 18.6.2 Enum exhaustiveness

For an enum scrutinee, the case construct is exhaustive when it has an irrefutable case *or* the set of covered variants equals the full set of variant names (`CoveredVariants(cases, E) = VariantNames(E)`). Otherwise — with no `else` — it is a compile-time error:

```text
(IfCase-Enum-NonExhaustive)
Γ; R; L ⊢ e : TypePath(p)    EnumDecl(p) = E    else_opt = ⊥
¬(HasIrrefutableCase(cases, TypePath(p)) ∨ CoveredVariants(cases, E) = VariantNames(E))
───────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ IfCaseExpr(e, cases, else_opt) ⇑ Code(IfCase-Enum-NonExhaustive)   -- E-SEM-2741
```

#### 18.6.3 Modal exhaustiveness

For a general modal-reference scrutinee, the covered states must equal `States(M)` (or an irrefutable case must be present):

```text
(IfCase-Modal-NonExhaustive)
Γ; R; L ⊢ e : ModalRefType(modal_ref)    ModalDeclOf(modal_ref) = M    else_opt = ⊥
¬(HasIrrefutableCase(cases, ModalRefType(modal_ref)) ∨ CoveredStates(cases, modal_ref) = States(M))
───────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ IfCaseExpr(e, cases, else_opt) ⇑ Code(IfCase-Modal-NonExhaustive)
```

For `Outcome`, the two variants `Outcome::Value` and `Outcome::Error` must both be covered; covering both is exhaustive, which is why the §18.3.6 example needs no `else`. Enum non-exhaustiveness is reported by `E-SEM-2741`/`E-SEM-2705` (§17.7).

#### 18.6.4 Union exhaustiveness

For a union scrutinee `T_1 | … | T_n`, exhaustiveness requires that every member type be covered by a `CoversMember` case — i.e. an irrefutable pattern for that member, in practice a typed pattern `_ : T_i` / `x : T_i` with the exact member type — or that an irrefutable case for the whole union be present:

```text
UnionTypesExhaustive(cases, types) ⇔
    ∀ T ∈ types. ∃ ⟨p, b⟩ ∈ cases. CoversMember(p, T)

(IfCase-Union-NonExhaustive)
Γ; R; L ⊢ e : TypeUnion([T_1, …, T_n])    else_opt = ⊥
¬(HasIrrefutableCase(cases, TypeUnion([T_1, …, T_n]))
  ∨ UnionTypesExhaustive(cases, [T_1, …, T_n]))
─────────────────────────────────────────────────────────
Γ; R; L ⊢ IfCaseExpr(e, cases, else_opt) ⇑ Code(IfCase-Union-NonExhaustive)   -- E-SEM-2705
```

#### 18.6.5 Other scrutinees

For all other scrutinee types (integers, strings, characters, booleans, tuples, records, and so on) the language does *not* attempt structural value-space exhaustiveness. Such a construct is well-typed only if it contains an irrefutable case or has an `else` (`T-IfCase-Other`):

```text
(T-IfCase-Other)
Γ; R; L ⊢ e : T_s    …    (else_opt ≠ ⊥ ∨ HasIrrefutableCase(cases, T_s))
─────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ IfCaseExpr(e, cases, else_opt) : T_r
```

In practice this means: match integers and strings with a trailing identifier/wildcard arm or an `else`; let enums, modals, and unions be covered case-by-case.

#### 18.6.6 Reachability

A case clause is **unreachable** when an earlier clause already covers everything it would, and is a compile-time error. Two situations trigger it:

- An earlier clause's pattern is irrefutable for the scrutinee type (it matches everything, so nothing after it is reachable), or
- An earlier clause carries the *same case label* — `CaseLabel(EnumPattern(path, v, _)) = ⟨enum, path, v⟩`, `CaseLabel(ModalPattern(s, _)) = ⟨modal, s⟩`, or `CaseLabel(TypedPattern(_, T)) = ⟨union, T⟩` — as this clause.

```text
CaseUnreachable(T, cases, i) ⇔
    (∃ j. 1 ≤ j < i ∧ Irrefutable(cases[j].pat, T))
  ∨ (CaseLabel(cases[i].pat) ≠ ⊥
       ∧ ∃ j. 1 ≤ j < i ∧ CaseLabel(cases[j].pat) = CaseLabel(cases[i].pat))

(IfCase-Unreachable)
Γ; R; L ⊢ e : T_s    1 ≤ i ≤ |cases|    CaseUnreachable(T_s, cases, i)
──────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ IfCaseExpr(e, cases, else_opt) ⇑ Code(IfCase-Unreachable)   -- E-SEM-2751
```

Because only `EnumPattern`, `ModalPattern`, and `TypedPattern` produce a non-`⊥` `CaseLabel` (every other pattern's label is `⊥`), the duplicate-label check applies to variant/state/union dispatch; literal and range arms are not flagged as duplicate labels (though an earlier irrefutable arm still shadows them).

#### 18.6.7 How exhaustiveness drives safe code

Exhaustiveness turns "did you handle every case?" from a review question into a compile-time guarantee:

- **No silent fall-through.** Without an `else`, a structured match that omits a variant/state/member does not compile. You cannot accidentally drop the error case from an `Outcome`, or forget a modal state.
- **Refactors are checked.** Adding a variant to an enum, a state to a modal, or a member to a union immediately surfaces every now-incomplete match as `E-SEM-2741` / `IfCase-Modal-NonExhaustive` / `E-SEM-2705`, so you find every site to update.
- **The panic arm is unreachable in safe matches.** Since a no-`else` enum/modal/union match is accepted only when proven exhaustive, the implicit `LowerPanic(MatchFail)` arm of §18.5.4 can never fire for those scrutinees.
- **Dead arms are rejected.** Reachability (`E-SEM-2751`) prevents stale or duplicated clauses from masking later logic.

#### 18.6.8 Worked example — refactor safety

```ultraviolet
public modal Door {
    @Closed {
    }
    @Open {
        public opened_at: u64
    }
    @Locked {
        public key_id: u32
    }
}

/// Total, exhaustive classification of every door state.
public procedure isSecured(door: Door) -> bool {
    return if door is {
        @Closed {
            false
        }
        @Open { opened_at } {
            false
        }
        @Locked { key_id } {
            true
        }
    }
}
```

If a new state `@Sealed { ... }` is later added to `Door`, the modal exhaustiveness rule rejects `isSecured` until the new state is handled — the type system makes the omission impossible to ship.

---

### 18.7 Idioms & Best Practices

- **Model state in types, then match it.** Per the style guide, prefer `modal` types over booleans or status flags for lifecycle state, and use `if ... is { ... }` to dispatch on state. Modal-pattern narrowing makes state-specific fields and transitions available exactly where they are valid, with no runtime re-validation.
- **Match `Outcome` with both variants.** Always handle `Outcome::Value` and `Outcome::Error` explicitly. This is exhaustive without an `else` and makes failure handling structurally mandatory — the safe-by-default consumption of fallible results.
- **Let exhaustiveness work for you; avoid catch-all `else` on enums and modals.** A trailing `else` (or a bare identifier/wildcard arm) suppresses the non-exhaustiveness diagnostic, which *defeats* the refactor-safety guarantee. Reserve `else`/`_`/identifier arms for scrutinee categories where the compiler does not check value-space exhaustiveness (integers, strings, ranges), where they are required.
- **Use field shorthand for clean destructuring.** Write `Point { x, y }` rather than `Point { x: x, y: y }`. Use the explicit `name : pattern` form only when you are testing a field value or further deconstructing it.
- **Prefer descriptive binding names.** Bound names follow local-variable naming: `snake_case`, descriptive, no type encoding (`endpoint`, `bytes_sent`, not `s` or `n_val`). Enum variants and modal states are `PascalCase` by declaration.
- **Order clauses from most specific to most general.** Put literal/precise patterns before identifier or wildcard arms; an earlier irrefutable arm makes everything after it unreachable (`E-SEM-2751`).
- **Keep arm bodies legible.** Each arm body is a `block_expr`. For multi-condition logic that cannot be a single pattern, bind in the pattern and use an inner `if` for the guard rather than overloading the pattern.
- **Single-case `if ... is ... else` for type tests.** When you only care about one shape, `if value is _ : T { ... } else { ... }` narrows the binding on both branches; reach for the case-list form when there are several shapes to dispatch.
- **Use `..` versus `..=` deliberately.** Half-open `..` excludes the upper bound; `..=` includes it. Choose the form that states your intent without off-by-one arithmetic.
- **Remember strings do not concatenate.** There is no `+` on `string` and no interpolation. When an arm must produce a message, return a string literal per arm, or return an already-`string` binding; build composite text through the dedicated string built-ins, not pattern arms.

---

### 18.8 Pitfalls & Diagnostics

The pattern subsystem defines the following named diagnostics. All are detected at compile time with `Error` severity.

| Code         | Condition |
| ------------ | --------- |
| `E-SEM-2705` | `if ... is { ... }` case analysis is not exhaustive for a **union** scrutinee (`IfCase-Union-NonExhaustive`). |
| `E-SEM-2711` | A **refutable** pattern was used in an irrefutable context such as `let` (`Let-Refutable-Pattern-Err`). |
| `E-SEM-2713` | A **duplicate binding identifier** appears within a single pattern (`Pat-Dup-Err`). |
| `E-SEM-2721` | A **range-pattern bound is not a compile-time constant** (`RangePattern-NonConst`). |
| `E-SEM-2722` | A **range pattern's start exceeds its end** (empty range) (`RangePattern-Empty`). |
| `E-SEM-2731` | A **record pattern references a non-existent field** (`RecordPattern-UnknownField`). |
| `E-SEM-2741` | `if ... is { ... }` case analysis is not exhaustive for an **enum** scrutinee (`IfCase-Enum-NonExhaustive`). |
| `E-SEM-2751` | A **case clause is unreachable** (`IfCase-Unreachable`). |
| `E-SEM-2761` | A **bare type name** appears in an `if ... is` pattern; use `: T` or `_ : T` instead (`IfIs-BareTypePattern-Err`). |
| `E-SEM-2762` | A **typed `if ... is` pattern is incompatible** with the scrutinee type (`IfIs-TypedPattern-Incompatible`). |

Additional checks not carrying an `E-SEM` table number include the non-exhaustive **general-modal** scrutinee error (`IfCase-Modal-NonExhaustive`), tuple-pattern arity mismatch (`Pat-Tuple-Arity-Err`, §18.2), and ambiguous `A::B { ... }` resolution (`E-MOD-1307`, §18.3).

Common pitfalls and how to avoid them:

- **Refutable pattern in `let` (`E-SEM-2711`).** Only irrefutable patterns may appear in a `let`/`var` binding or `loop pattern in` variable. Literal, enum, modal, and range patterns are refutable and rejected there:

  ```text
  (Let-Refutable-Pattern-Err)
  pat ∈ {LiteralPattern(_), EnumPattern(_, _, _), ModalPattern(_, _), RangePattern(_, _, _)}
  ───────────────────────────────────────────────────────────────────────────────────────────
  Γ ⊢ pat ⇐ T ⇑ Code(Let-Refutable-Pattern-Err)
  ```

  Destructure such values with `if ... is`, not `let`. Tuple and record patterns are allowed in `let` only when *all* their sub-patterns are irrefutable.

- **Bare type name in `if ... is` (`E-SEM-2761`).** A bare identifier in case position that resolves to a type name is rejected — it is ambiguous with an ordinary binding. Write the type test explicitly as `: T` (shorthand for `_ : T`) or `name : T`. A bare identifier always means "bind this name," never "test this type."

- **Incompatible typed pattern (`E-SEM-2762`).** A typed pattern `x : T_a` must match the scrutinee *exactly*: `T_a` must be `≡` the (stripped) scrutinee type or, for a union scrutinee, `≡` exactly one of its members. A type that is merely a subtype or an unrelated type is rejected (`Γ ⊢ TypedPattern(x, T_a) ◁ T_s` is undefined).

- **Duplicate binding (`E-SEM-2713`).** A single pattern cannot bind the same name twice (`¬ Distinct(PatNames(pat))`). `(x, x)` and `Pair { first: x, second: x }` are both errors; rename one binding.

- **Refutable payload does not cover (§18.6.1).** `Shape::Circle(0.0)` matches only circles of radius zero, so it does *not* count toward exhaustiveness for the `Circle` variant. To cover a variant or state, give its payload an irrefutable sub-pattern (`Shape::Circle(radius)` or `Shape::Circle(_)`). Otherwise you will still need another clause (or `else`) and may trip `E-SEM-2741` / the modal/union equivalents.

- **Unknown record field (`E-SEM-2731`).** Every field named in a record or payload pattern must exist on the type (`FieldName(fp) ∈ FieldNameSet(R)`) and be visible at the use site. A typo in a field name is an error, not a silently ignored extra binding.

- **Empty or non-constant ranges (`E-SEM-2722`, `E-SEM-2721`).** `5 ..= 3` is statically empty and rejected; `lo ..= hi` with non-literal bounds is rejected because range bounds must be compile-time integer constants (`ConstPatInt`). Compute dynamic bounds with explicit comparisons in an arm body, not with a range pattern.

- **Unreachable clause (`E-SEM-2751`).** Placing a wildcard, identifier, or otherwise irrefutable arm before more specific arms makes the later arms unreachable, as does repeating the same variant/state/union label. Order specific cases first and remove duplicates.

- **Single-element tuple spelling.** A one-element tuple pattern is `(p;)` with a semicolon — *not* `(p)`. The `(p)` form inside an enum payload is a one-element tuple *enum payload*, where the semicolon is forbidden; do not confuse the two contexts.

- **Strings have no `+`.** A pattern arm cannot build a message by concatenation — `+` requires two numeric primitive operands (`(T-Arith)`), and there is no `string::concat` or interpolation. Return a string literal, the bound `string`, or a numeric value from each arm.
