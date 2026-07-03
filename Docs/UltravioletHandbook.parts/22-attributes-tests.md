## 22. Attributes, Metadata & Source-Native Tests

Ultraviolet exposes a single, uniform attribute system. One concrete syntax — the `#`-prefixed attribute — drives layout control, optimization hints, diagnostics metadata, dynamic-verification scoping, reflection and code-generation scheduling, FFI metadata, and source-native tests. There is no separate annotation, pragma, or decorator dialect: every metadata facility in the language is an *attribute* parsed by one grammar (§9.1) and validated by the section that owns its name.

This chapter covers the attribute grammar and placement rules (§9.1), vendor-scoped attributes (§9.2), layout attributes (§9.3), optimization attributes (§9.4), diagnostics-and-metadata attributes including `#dynamic` (§9.5), and source-native test attributes including the `#test` runner contract (§9.6). FFI-specific attributes (`#mangle`, `#library`, `#unwind`, `#export`, `#host_export`, `#ffi_pass_by_value`) are named here for completeness but their semantics belong to the FFI chapter (§23.4); the Phase-2 attributes `#reflect`, `#derive`, `#emit`, and `#files` are introduced here and detailed in the metaprogramming chapter (Chapter 22 of the specification).

A note on notation before anything else: the project style guide refers to the dynamic-verification attribute informally as `[[dynamic]]`. That double-bracket form is a *prose shorthand* only. The normative surface syntax — the form that actually parses and compiles — is the single-`#` attribute `#dynamic`. Ultraviolet has no `[[...]]` attribute delimiter. Everywhere this handbook writes attribute source, it uses the `#` form.

### 22.1 Attribute Syntax and Placement (§9.1)

#### 22.1.1 The attribute grammar

An attribute begins with the operator `#`, followed by an attribute *spec*: a name and an optional parenthesized argument list. One or more attributes form an attribute list. The canonical grammar (§9.1.1, restated verbatim in Appendix B.8) is:

```ebnf
attribute_list    ::= attribute+
attribute         ::= "#" attribute_spec
attribute_spec    ::= attribute_name ("(" attribute_args? ")")?
attribute_name    ::= identifier
                    | "dynamic"
                    | "static"
                    | vendor_prefix "::" identifier
                    | vendor_prefix "::" "dynamic"
                    | vendor_prefix "::" "static"
vendor_prefix     ::= identifier ("::" identifier)*
attribute_args    ::= attribute_arg ("," attribute_arg)* ","?
attribute_arg     ::= literal
                    | identifier
                    | identifier ":" literal
                    | identifier ":" identifier
                    | identifier "(" attribute_args? ")"
```

Key structural facts that follow directly from this grammar:

- The leading token is the operator `#`. The same `#` token is also the key-marker (the key system uses `key_marker ::= "#"` for key fields and key indices); the parser disambiguates by position. In attribute position, `#` is always immediately followed by an attribute name.
- The argument list is optional twice over: an attribute may carry no parentheses at all (`#cold`), empty parentheses (the parser admits an empty `( )` argument list, though no specification attribute accepts that form), or a non-empty list.
- A trailing comma is permitted inside `attribute_args` (`#derive(Eq, Clone,)` parses).
- Arguments come in five shapes: a bare literal, a bare identifier, a `name: literal` pair, a `name: identifier` pair, and a nested call `name(args)`. The named-pair form uses `:` (colon), not `=`.
- The reserved words `dynamic` and `static` are admitted *only* in the leaf (final) position of an attribute name. They MUST NOT appear inside a `vendor_prefix`. This is why `#dynamic`, `#static`, and `#acme::dynamic` are legal but `#dynamic::frobnicate` is not.

#### 22.1.2 Placement

> An attribute list MUST appear immediately before the declaration or expression it modifies. (§9.1.1)

There is no free-floating or "inner" attribute form. Every attribute attaches to exactly one syntactic target that immediately follows it. The declaration grammar (Appendix B.6) threads an optional `attribute_list?` as the first element of every attributable production: `import_decl`, `using_decl`, `static_decl`, `procedure_decl`, `method_def`, `record_decl`, `record_field_decl`, `enum_decl`, `modal_decl`, `state_field_decl`, `state_method_def`, `transition_def`, `class_declaration`, `abstract_field`, `type_alias_decl`, `extern_block`, `foreign_procedure`, and the `comptime` forms (`comptime_stmt`, `comptime_expr`, `comptime_procedure_decl`).

Expressions are attributed through the expression grammar (Appendix B.3):

```ebnf
expression         ::= attributed_expr | unattributed_expr
attributed_expr    ::= attribute_list expression
```

So an attribute placed before an expression binds to that whole expression.

The set of legal *targets* — the kinds of construct an attribute may modify — is fixed (§9.1.4):

```text
AttrTarget = {Record, Enum, Modal, Procedure, Method, Field, Binding,
              Statement, Expression, KeyBlock, ExternBlock, TypeAlias}
```

Each registered attribute name declares the subset of these targets it is valid on. The registry and its target map (§9.1.4) are:

```text
R_spec = {
  layout, inline, cold, deprecated,
  dynamic, stale_ok,
  relaxed, acquire, release, acqrel, seqcst,
  static,
  mangle, library, unwind,
  reflect, derive, emit, files,
  export, host_export, ffi_pass_by_value,
  test
}

AttrTargets(layout)            = {Record, Enum}
AttrTargets(inline)            = {Procedure, Method}
AttrTargets(cold)              = {Procedure, Method}
AttrTargets(deprecated)        = {Record, Enum, Modal, Procedure, Method, Field, Binding, TypeAlias}
AttrTargets(dynamic)           = {Record, Enum, Modal, Procedure, Method, Expression}
AttrTargets(stale_ok)          = {Binding}
AttrTargets(relaxed)           = {Expression, KeyBlock}
AttrTargets(acquire)           = {Expression, KeyBlock}
AttrTargets(release)           = {Expression, KeyBlock}
AttrTargets(acqrel)            = {Expression, KeyBlock}
AttrTargets(seqcst)            = {Expression, KeyBlock}
AttrTargets(static)            = {Procedure}
AttrTargets(mangle)            = {Procedure}
AttrTargets(library)           = {ExternBlock}
AttrTargets(unwind)            = {Procedure}
AttrTargets(reflect)           = {Record, Enum, Modal}
AttrTargets(derive)            = {Record, Enum, Modal}
AttrTargets(emit)              = {Statement, Expression}
AttrTargets(files)             = {Statement, Expression}
AttrTargets(export)            = {Procedure}
AttrTargets(host_export)       = {Procedure}
AttrTargets(ffi_pass_by_value) = {Record, Enum}
AttrTargets(test)              = {Procedure}
```

#### 22.1.3 Well-formedness

For every declaration or expression with attribute list `A` and target kind `τ`, the implementation MUST check `Γ ⊢ AttrListWf(A, τ) ⇓ ok` (§9.1.4). A list is well-formed when every entry's name is registered, every entry is valid on `τ`, and every entry's arguments satisfy that attribute's argument grammar:

```text
(AttrList-Ok)
A = [a_1, …, a_n]   ∀ i, a_i = ⟨name_i, args_i⟩
∀ i, name_i ∈ R_spec ∪ R_vendor   ∀ i, τ ∈ AttrTargets(name_i)   ∀ i, AttrArgsOk(name_i, args_i)
────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ AttrListWf(A, τ) ⇓ ok
```

Three derived rules govern the failure cases:

- **Malformed attribute syntax is ill-formed** → `E-MOD-2450`.
- An attribute name not in `R_spec ∪ R_vendor` raises `Attr-Unknown` → `E-MOD-2451`. (And `R_vendor = ∅`; see §22.2.)
- A registered attribute applied to a target kind outside its `AttrTargets` raises `Attr-Target-Err` → `E-MOD-2452`.

Two composition rules complete the picture:

- **Multiple attribute lists on the same target** are equivalent to a single list with the entries concatenated in source order. Application order is left-to-right in that concatenated list.
- Attribute lists do not evaluate to runtime values. Any runtime effect is contributed by the owning attribute family, never by the attribute syntax itself (§9.1.5).

The memory-order attributes (`relaxed`, `acquire`, `release`, `acqrel`, `seqcst`) are well-formed only when attached to a key block or an expression that contains a key acquisition; their semantics belong to the concurrency/key chapter (the surface form is `memory_order_attribute ::= "#" memory_order` from Appendix B.10).

#### 22.1.4 Worked example

```ultraviolet
// Two separate attribute lists on one record are equivalent to one
// concatenated list, applied left-to-right: layout(C) then reflect.
#layout(C)
#reflect
public record PixelRgba8 {
    public r: u8
    public g: u8
    public b: u8
    public a: u8
}

// A named-argument attribute. Note the ':' separator, not '='.
#deprecated("use renderFrameV2 instead")
public procedure renderFrame(request: FrameRequest) -> FrameReply {
    return renderFrameV2(request)
}
```

### 22.2 Vendor Attributes (§9.2)

Vendor attributes let third parties define attribute names without colliding with the specification namespace. They introduce no new concrete syntax beyond the scoped `attribute_name` form already in §9.1.1: a vendor-qualified name is a `vendor_prefix` of one or more `::`-separated identifier segments, then `::`, then a leaf that is an `identifier` (or, exceptionally, the reserved verification-mode leaf `dynamic` or `static`).

Static rules (§9.2.4):

- Vendor-defined attributes MUST use a scoped prefix of the form `vendor::name` or `com::vendor::name`.
- The `ultraviolet::...` namespace is reserved for specification-defined attributes. User code that writes a name under `ultraviolet::...` is rejected with `E-CNF-0402` (`Intro-Reserved-Ultraviolet-Err`).
- In this version of the language `R_vendor = ∅`. Because the registry of vendor attributes is empty, *every* attribute name that is not in `R_spec` is rejected as unknown via `E-MOD-2451` (§9.1.7). In other words: scoped vendor names parse, but there is currently no registered vendor attribute, so writing one is a compile error today.

The practical consequence: do not invent vendor attributes expecting the compiler to ignore them. They will not be ignored — they will fail as unknown. Vendor scoping exists so that, when a vendor registry becomes populated, those names cannot clash with `R_spec` or with `ultraviolet::...`.

```ultraviolet
// Parses as a scoped vendor name (prefix = acme::tooling, leaf = audit).
// Currently rejected: R_vendor is empty, so the name is "unknown" (E-MOD-2451).
#acme::tooling::audit
public procedure exportLedger(authority: $IO) -> Ledger {
    return buildLedger(authority)
}
```

### 22.3 Layout Attributes (§9.3)

Layout attributes give explicit control over the in-memory representation and ABI of a `record` or an `enum`. They are the language's tool for C interoperability, field packing, and forced alignment.

#### 22.3.1 Syntax

```ebnf
layout_attribute ::= "#" "layout" "(" layout_args ")"
layout_args      ::= layout_kind ("," layout_kind)*
layout_kind      ::= "C" | "packed" | "align" "(" integer_literal ")" | int_type
int_type         ::= "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64"
```

`#layout` always takes a parenthesized, non-empty list of layout kinds. It is valid only on `Record` and `Enum` targets.

#### 22.3.2 The layout kinds

**`#layout(C)`** — C-compatible layout.
- For a `record`: fields are laid out in declaration order; padding is inserted only as the target's C ABI requires; total size is a multiple of the record's alignment.
- For an `enum`: the discriminant is a C-compatible integer tag; the default tag type is the enum's `DiscType(E)` (§12.7); the whole layout conforms to a tagged union under the target C ABI.

**`#layout(IntType)`** — explicit enum discriminant. Valid *only* on `enum`. The discriminant uses the named integer type (`i8`–`i64` or `u8`–`u64`), and every variant's discriminant value MUST be representable in that type.

**`#layout(packed)`** — remove inter-field padding. Valid *only* on `record`. All inter-field padding is removed, every field is laid out at alignment 1, and the record's overall alignment becomes 1.
- Critical safety rule: **taking a reference to a packed field MUST occur within an `unsafe` block**. Outside `unsafe`, the program is ill-formed (`E-TYP-2105`, `Packed-Field-Unsafe-Err`). A misaligned reference is the hazard being guarded.

**`#layout(align(N))`** — raise minimum alignment.
- `N` MUST be a positive integer that is a power of two.
- Effective alignment is `max(N, natural alignment)`; if `N` is below the natural alignment, the natural alignment is used (and a warning `W-MOD-2451` is emitted).
- The type's size is padded to a multiple of the effective alignment.

#### 22.3.3 Valid and invalid combinations

The spec enumerates the legal argument combinations (§9.3.4):

```text
Valid combinations:                    Invalid combinations:
  layout(C)                              layout(packed, align(N))
  layout(packed)
  layout(align(N))
  layout(C, packed)
  layout(C, align(N))
  layout(u8)

Applicability:
  record : C, packed, align(N)
  enum   : C, align(N), IntType
  modal  : none
  generic (unmonomorphized) : none
```

The hard constraints:
1. `layout(packed)` on a non-`record` declaration is ill-formed → `E-MOD-2454`.
2. `layout(align(N))` where `N` is not a power of two is ill-formed → `E-MOD-2453`.
3. Conflicting layout arguments — notably `layout(packed, align(N))` — are ill-formed → `E-MOD-2455`.
4. `layout(align(N))` where `N < natural alignment` is a warning → `W-MOD-2451`.

Layout attributes contribute no direct runtime mechanism; they constrain the ABI and layout calculation performed during lowering. Packed-field reference safety is enforced statically at the use site, not at the declaration.

#### 22.3.4 Worked example

```ultraviolet
// C-compatible record for FFI: fields in declaration order, C-ABI padding.
#layout(C)
public record Vec3 {
    public x: f32
    public y: f32
    public z: f32
}

// Explicit one-byte discriminant on an enum (valid only on enum).
#layout(u8)
public enum BlendMode {
    Opaque = 0
    Alpha = 1
    Additive = 2
}

// Packed record: every field at alignment 1, no inter-field padding.
#layout(packed)
public record WireHeader {
    public version: u8
    public flags: u16
    public length: u32
}

// Forced 64-byte alignment, e.g. to land on a cache line.
#layout(align(64))
public record CacheLineCounters {
    public hits: u64
    public misses: u64
}
```

Taking a reference into the packed record requires `unsafe`. The address-of operator `&` yields a safe `Ptr<u16>@Valid`, not a raw pointer; the packed-field hazard is what forces the `unsafe` boundary:

```ultraviolet
public procedure readWireFlags(header: WireHeader) -> u16 {
    // &header.flags is a packed-field reference: it MUST be taken in `unsafe`.
    unsafe {
        let flags_ref: Ptr<u16>@Valid = &header.flags
        return *flags_ref
    }
}
```

### 22.4 Optimization Attributes (§9.4)

Optimization attributes are hints attached to `Procedure` or `Method` declarations. They never change the language-level runtime semantics of the annotated procedure; they only steer the implementation's code-generation choices.

#### 22.4.1 Syntax

```ebnf
inline_attribute ::= "#" "inline" ("(" inline_mode ")")?
inline_mode      ::= "always" | "never" | "default"

cold_attribute   ::= "#" "cold"
```

#### 22.4.2 Semantics

- **`#inline`** — the implementation SHOULD inline the procedure at call sites when feasible.
- **`#inline(always)`** — the implementation SHOULD inline at all call sites. If inlining is impossible (recursive procedures, or procedures whose address is taken), the implementation SHOULD emit warning `W-MOD-2452`.
- **`#inline(default)`** — equivalent to omitting the attribute entirely.
- **`#inline(never)`** — the implementation MUST NOT inline the procedure; its body MUST be emitted as a separate callable unit. This is the one inline mode with a `MUST`.
- **`#cold`** — marks a procedure as unlikely to run during typical execution. The implementation MAY use this as a code-layout or optimization hint.

Because `#inline` and `#cold` carry only hints (except the `MUST NOT` of `#inline(never)`), they are safe to add and remove without changing observable behavior.

#### 22.4.3 Worked example

```ultraviolet
#inline(always)
public procedure clampUnit(value: f32) -> f32 {
    if value < 0.0 {
        return 0.0
    }
    if value > 1.0 {
        return 1.0
    }
    return value
}

#inline(never)
public procedure reportFatal(message: string, authority: $IO) -> ! {
    logFatal(message, authority)
    abortProcess()
}

#cold
public procedure handleCorruptedFrame(frame: FrameRequest) -> FrameReply {
    return FrameReply::Skip
}
```

### 22.5 Diagnostics and Metadata Attributes (§9.5)

This family carries no new syntax of its own — every member uses the general attribute grammar of §9.1.1. It groups the deprecation marker, the dynamic-verification scope marker, the staleness suppressor, the foreign-contract verification mode, and the four Phase-2 metaprogramming attributes.

#### 22.5.1 `#deprecated`

`#deprecated` marks a declaration as deprecated. When that declaration is referenced, the implementation MUST emit a deprecation warning (`W-CNF-0601`). If a message argument is present, the diagnostic SHOULD include it. Its informative shape (Appendix B.8) is:

```ebnf
deprecated_attribute ::= "#" "deprecated" ("(" string_literal ")")?
```

Note the argument is a bare positional string literal, not a `name:` pair. Valid targets: `Record`, `Enum`, `Modal`, `Procedure`, `Method`, `Field`, `Binding`, `TypeAlias`.

#### 22.5.2 `#dynamic` — runtime verification scope

`#dynamic` marks a declaration or expression as requiring *runtime* verification in the cases where *static* verification is insufficient. It is the deliberate boundary tool the style guide refers to (in prose) as `[[dynamic]]`; the compiling form is `#dynamic`. Its shape is `dynamic_attribute ::= "#" "dynamic"` (Appendix B.8).

Valid targets: `Record`, `Enum`, `Modal`, `Procedure`, `Method`, `Expression`.

Scope determination (§9.5.4):
1. An expression `e` is within a `#dynamic` scope if it is lexically enclosed by a `#dynamic` declaration or by a `#dynamic`-attributed expression.
2. Scope is **lexical**. It does **not** propagate through procedure calls. Calling a `#dynamic` procedure from outside that procedure's own span does not place the call site in a dynamic context.
3. A class procedure's `#dynamic` annotation **does** propagate to overriding implementations (rule *DynamicContext-Override*): an override of a `#dynamic` class procedure executes in a dynamic context.

Effects of being in a dynamic scope: runtime synchronization (keys, Chapter 19), runtime contract checks (§15.8), and runtime refinement-type checks (§14.8) MUST be inserted *exactly* in the cases the owning chapters require, and MUST NOT be inserted otherwise. If a `#dynamic` scope results in no inserted runtime checks or synchronization, the implementation SHOULD emit `W-CON-0401`.

Target restrictions — these are *errors*, not warnings:
1. `#dynamic` on a contract predicate expression directly is ill-formed → `E-CON-0410`.
2. `#dynamic` on a `type` alias declaration is ill-formed → `E-CON-0411`.
3. `#dynamic` on a field declaration is ill-formed → `E-CON-0412`.

(These restrictions hold even though `#deprecated` *can* go on a field or type alias; `#dynamic` and `#deprecated` have different, independently-checked target sets.)

```ultraviolet
// Static verification of the contract may be insufficient here;
// #dynamic authorizes the runtime checks the contract chapter requires.
// The precondition precedes |=; the postcondition follows it and uses @result.
#dynamic
public procedure withdraw(account: Account, amount: u64) -> Account
    |: amount <= account.balance |= @result.balance == account.balance - amount {
    return Account { balance: account.balance - amount }
}
```

#### 22.5.3 `#stale_ok`

`#stale_ok` suppresses staleness warnings for bindings derived from `shared` data across `release` or `yield release` boundaries. It is valid only on `let` and `var` bindings (target kind `Binding`). It suppresses warnings only; it has no effect on lowering. Its detailed semantics belong to the concurrency (Chapter 19) and async (Chapter 21) chapters.

```ebnf
stale_ok_attribute ::= "#" "stale_ok"
```

#### 22.5.4 `#static` — verification mode

`#static` is interpreted only in foreign-contract contexts; its semantics are defined by the FFI verification chapter (§23.6). Together with `#dynamic`, it forms the two foreign-contract verification modes (`ffi_verification_mode ::= "static" | "dynamic"`, Appendix B.13). Its only valid target is `Procedure`.

#### 22.5.5 Phase-2 attributes: `#reflect`, `#derive`, `#emit`, `#files`

These four schedule or enable compile-time (Phase 2) work and introduce no direct Phase-4 runtime instrumentation. Their full semantics live in the metaprogramming chapter; here is the binding metadata each one carries:

- **`#reflect`** — marks a `record`, `enum`, or `modal` declaration as reflectable. A conforming implementation MUST expose the declaration's canonical shape, member order, and attached attributes to the compile-time reflection environment.
  ```ebnf
  reflect_attribute ::= "#" "reflect"
  ```
- **`#derive(...)`** — schedules derive-target execution in Phase 2 for the annotated `record`, `enum`, or `modal`. The argument list is a comma-separated list of derive-target identifiers.
  ```ebnf
  derive_attribute   ::= "#" "derive" "(" derive_target_list ")"
  derive_target_list ::= identifier ("," identifier)*
  ```
- **`#emit`** — grants the `TypeEmitter` capability to the annotated compile-time statement or expression. The target MUST be a `comptime` form.
  ```ebnf
  emit_attribute ::= "#" "emit"
  ```
- **`#files`** — grants the `ProjectFiles` capability to the annotated compile-time statement or expression. The target MUST be a `comptime` form.
  ```ebnf
  files_attribute ::= "#" "files"
  ```

```ultraviolet
// #reflect exposes shape + members + attributes to compile-time reflection.
// #derive schedules the Eq and Clone derive targets in Phase 2.
#reflect
#derive(Eq, Clone)
public record Glyph {
    public codepoint: u32
    public advance: f32
}
```

`#emit` and `#files` may attach only to `comptime` forms (`comptime_stmt ::= attribute_list? "comptime" block_expr`). The capability granted by the attribute (`ProjectFiles`, `TypeEmitter`) and its API are defined by the metaprogramming chapter; the body below is illustrative of placement only:

```ultraviolet
#files
comptime {
    // The ProjectFiles capability granted by #files is used here; the exact
    // surface API is defined by the metaprogramming chapter.
    let manifest: string = readProjectFile("assets/manifest.uvtext")
    registerManifest(manifest)
}
```

### 22.6 Source-Native Test Attributes (§9.6)

Ultraviolet tests are not a bolt-on framework. A test is an ordinary procedure marked `#test`, living in source, type-checked and contract-checked like any other procedure, and run by the toolchain's `uv test` command. The procedure's *postcondition* is the assertion: the runner judges pass/fail by whether the postcondition holds after the procedure returns.

#### 22.6.1 Syntax

```ebnf
test_attribute      ::= "#" "test" ("(" test_attribute_args ")")?
test_attribute_args ::= test_attribute_arg ("," test_attribute_arg)*
test_attribute_arg  ::= "name" ":" string_literal
                      | "covers" "(" string_literal ")"
```

`#test` is valid only on a `Procedure`. It accepts at most one `name:` argument (a display label) and any number of `covers(...)` arguments (audit-coverage references).

#### 22.6.2 Identity and coverage

A source-native test is just a `ProcedureDecl` whose `AttrByName(proc, test)` set is non-empty. The runner derives:

```text
TestName(proc) = s                           when a unique `name: s` argument is present.
TestName(proc) = FullyQualifiedProcPath(proc) when no `name` argument is present.

TestCoverage(proc) = [r_1, …, r_n]           where each r_i is the string argument of one
                                              covers(r_i) entry, in source order.
```

The fully-qualified procedure path is the **stable test identity**; `name: "..."` is only a display label. Discovery order is module path, then file order, then declaration span, then fully-qualified procedure symbol.

Each `covers(...)` reference must name one row in the obligation ledger using the form `obligation-id@Linternal_spec_line` (§9.6.4, constraint 4); an unknown coverage reference is `E-TST-0107`.

#### 22.6.3 Argument well-formedness

`AttrArgsOk(test, args)` holds exactly when (§9.6.4):

1. every argument is either `name: string_literal` or `covers(string_literal)`;
2. at most one `name` argument is present (a second is `E-TST-0102`);
3. every `covers` argument has exactly one non-empty string literal argument (otherwise `E-TST-0103`);
4. every coverage reference names a real obligation-ledger row.

A malformed `#test` argument generally is `E-TST-0101`.

#### 22.6.4 Required test-procedure shape

A `#test` procedure MUST (§9.6.4):

1. have a body;
2. be non-generic;
3. have **explicit visibility**;
4. have an **explicit return type**;
5. have a contract clause containing a **postcondition**;
6. have either **no parameters** or **exactly one parameter** whose type is the toolchain-provided `TestAuthority` type.

`#test` applied to anything other than an ordinary source procedure is `E-TST-0109`; a procedure that violates the shape rules is `E-TST-0104`; a missing postcondition is specifically `E-TST-0106`; an invalid `TestAuthority` parameter is `E-TST-0105`.

The contract clause syntax used for the postcondition is the standard one (Appendix B.7):

```ebnf
contract_clause    ::= "|:" contract_body
contract_body      ::= precondition_expr "|=" postcondition_expr
                     | "|=" postcondition_expr
                     | precondition_expr
contract_intrinsic ::= decorated_identifier("@", "result")
                     | decorated_identifier("@", "entry") "(" expression ")"
```

A test typically uses the `"|=" postcondition_expr` form (a postcondition with no precondition), and references the procedure's result with the `@result` intrinsic. The `@result` and `@entry(...)` source spellings are `@` decorator sequences, not combined lexer tokens. Note that in the `procedure_decl` grammar the contract clause sits after the signature and before the body (`… signature contract_clause? block_expr`).

#### 22.6.5 The `TestAuthority` parameter

`TestAuthority` is the only runner-injected value. It carries the filesystem, process, temporary-directory, target-profile, compiler-invocation, and time authority needed by effectful compiler tests and benchmark-style tests. Per the style guide's capability-narrowing rule, take it **only** when the test actually needs an effect; a pure test takes no parameters.

Its `time` field has type `$Time`. The `$Time` capability exposes `monotonic()` (returning `$MonotonicTime`) and `wall()` (returning `$WallTime`). Tests that measure elapsed time SHOULD use `time~>monotonic()`; `time~>wall()` is for UTC behavior, not benchmarks. (`TestAuthority`, `Time`, `MonotonicTime`, `MonotonicInstant`, `Duration`, and `TimeError` are reserved built-in names; their interfaces are defined in the standard-library/time chapter.)

#### 22.6.6 Pass / fail / error semantics

`#test` does not change ordinary procedure execution. During a test run, the runner calls each discovered test procedure, and judges the outcome (§9.6.5):

- **Pass** — the procedure returns normally *and* its postcondition is satisfied.
- **Fail** — the procedure returns normally *and* its postcondition is violated.
- **Error** — the procedure is ill-formed for test execution, panics, requires unavailable authority, or cannot be invoked by the generated harness.

`#test` never lowers into production program artifacts.

#### 22.6.7 How `uv test` discovers and runs tests

Tests live under a *tests subtree*. An assembly `A` is test-bearing when one of its modules' paths is prefixed by `A.name :: Tests`:

```text
TestsPrefix(A)    = A.name :: Tests
TestBearing(A)    ⇔ ∃ m ∈ A.modules. Prefix(path(m), TestsPrefix(A))
TestAssemblies(P) = [A ∈ P.assemblies | TestBearing(A)]
```

`uv test` takes an optional positional argument that selects scope (§9.6.6):

```text
TestScope ::= AllTests | AssemblyTests(A) | ModuleTests(q) | SourceFileTests(f) | DirectoryTests(d)
```

Resolution of the argument (`ResolveTestTarget`):
- no argument → `AllTests`;
- a host path to a file → `SourceFileTests`;
- a host path to the project root directory → `AllTests`;
- a host path to a non-root directory → `DirectoryTests`;
- a non-path string matching an assembly name → `AssemblyTests`;
- a non-path string parsing as a module path that exists → `ModuleTests`;
- otherwise → `Test-Target-Err` (`E-TST-0108`).

For each selected assembly, `uv test` generates an ephemeral harness in that assembly's build output directory, compiles the assembly project with that harness as entrypoint, and invokes the selected tests in deterministic order. Only procedures inside the `Tests` subtree are selected.

#### 22.6.8 Complete `#test` example

A pure test (no authority) that asserts a property of a procedure under test. Note that the comparison literals carry explicit suffixes so each operand has the same type — an unsuffixed integer literal in synthesis position defaults to `i32`, which would mismatch a `u8`/`u32` operand:

```ultraviolet
public procedure addSaturatingU8(left: u8, right: u8) -> u8 {
    let sum: u32 = (left as u32) + (right as u32)
    if sum > 255u32 {
        return 255u8
    }
    return sum as u8
}

// A source-native test. It satisfies every shape requirement:
//  - has a body
//  - is non-generic
//  - has explicit visibility (public)
//  - has an explicit return type (bool)
//  - has a contract clause with a postcondition (the part after |=)
//  - takes no parameters (it needs no authority)
// The postcondition is the assertion: pass iff @result == true.
#test(name: "addSaturatingU8 saturates at the u8 ceiling")
public procedure testSaturationClampsToMax() -> bool
    |= @result == true {
    let result: u8 = addSaturatingU8(200u8, 100u8)
    return result == 255u8
}
```

An effectful, benchmark-style test that takes the single `TestAuthority` parameter and uses its `time` field. Authority is taken only because elapsed-time measurement genuinely needs it. `elapsed` returns `Outcome<Duration, TimeError>`, so the result is matched with `if … is` rather than discarded; the `Duration.nanoseconds` field has type `u128`, so the budget literal is suffixed `u128`:

```ultraviolet
#test(name: "buildIndex completes within budget")
public procedure testBuildIndexWithinBudget(authority: TestAuthority) -> bool
    |= @result == true {
    let clock: $MonotonicTime = authority.time~>monotonic()
    let start: MonotonicInstant = clock~>now()

    let index: SearchIndex = buildIndex()

    let stop: MonotonicInstant = clock~>now()

    // elapsed yields Outcome<Duration, TimeError>; match the Value variant.
    if clock~>elapsed(start, stop) is Outcome::Value(elapsed) {
        return elapsed.nanoseconds <= 5_000_000u128
    } else {
        return false
    }
}
```

### Idioms & Best Practices

- **Prefer the static formulation; reach for `#dynamic` deliberately.** The style guide is explicit: use the dynamic-verification attribute only when the intended semantics are truly dynamic. Do not use it to bypass correct static conformance, to compensate for weak type modeling, or to patch over a missing contract. If a static formulation matches the intended behavior, use it. Treat `#dynamic` (and `unsafe`) as boundary tools, not convenience escapes.
- **Keep `TestAuthority` out of pure tests.** Authority narrowing is part of API design, and tests are no exception. A test that asserts a pure property should take no parameters. Inject `TestAuthority` only when the test actually performs an effect (filesystem, process, time, compiler invocation). This keeps the test harness honest about which tests are effectful.
- **Use `name:` for human-facing labels, not for identity.** The fully-qualified procedure path is the stable test identity used for selection and reporting order. A `name:` argument is a display label; renaming it does not change which test runs. Give tests descriptive procedure names (`testSaturationClampsToMax`, `camelCase` per the naming matrix) so the default identity reads well even without a label.
- **Put layout attributes only where the ABI matters.** Reach for `#layout(C)` at FFI boundaries and `#layout(packed)`/`#layout(align(N))` only when an external format or a performance constraint demands it. Isolate ABI-facing types in boundary modules rather than letting representation control leak through ordinary code.
- **Add optimization attributes last, and sparingly.** `#inline` and `#cold` are hints; they should follow measurement, not precede it. `#inline(never)` is the only optimization attribute with a hard guarantee — use it when you specifically need a separately-emitted callable unit (for example, a fatal-error path or a procedure whose address must be taken).
- **Write the postcondition as the real assertion.** Because the runner judges a test by its postcondition, encode the property you actually want guaranteed in the contract, not in incidental control flow. A test whose body returns `true` but whose postcondition is `@result == true` makes the success condition explicit and machine-checkable.
- **Use `monotonic()` for timing, `wall()` for UTC.** This is a normative SHOULD. Benchmark and budget tests must measure with `time~>monotonic()`; `time~>wall()` is for behavior that genuinely depends on calendar time.

### Pitfalls & Diagnostics

- **Do not write `[[dynamic]]` in source.** The double-bracket form is documentation shorthand only. Ultraviolet has no `[[...]]` attribute delimiter; only the `#` form parses. Writing `[[dynamic]]` is a syntax error, not an attribute.
- **`if` bodies require braces.** The grammar is `if_tail ::= block_expr …`; a brace-less `if cond return x` does not parse. Every `if`/`else` arm must be a `{ … }` block (or a nested `if`/`is` form).
- **Enum literals use `::`, not `.`.** An enum variant value is `type_path "::" identifier` (e.g. `FrameReply::Skip`). Writing `FrameReply.Skip` parses as field access on a type, not an enum literal.
- **Unsuffixed integer literals synthesize to `i32`.** In synthesis position (such as a comparison operand) an unsuffixed integer literal types as `i32` (`DefaultInt`). Comparing it against a `u8`/`u32`/`u128` value is a type mismatch unless the literal is suffixed (`255u8`, `5_000_000u128`) or the literal sits in a checking position (a `return` against a known return type, a call argument against a known parameter type).
- **`&` yields a safe `Ptr`, not a raw pointer.** Address-of produces `Ptr<T>@Valid`. A raw-pointer type (`*imm T` / `*mut T`) is a different type and is not what `&place` produces.
- **Wrong target kind → `E-MOD-2452`.** Each attribute has a fixed target set. `#inline` on a record, `#layout` on a procedure, `#stale_ok` on anything but a `let`/`var` binding, or `#test` on a non-procedure all fail. `#test` outside an ordinary procedure has its own dedicated code, `E-TST-0109`.
- **Unknown / vendor names → `E-MOD-2451`.** Since `R_vendor = ∅`, any name outside `R_spec` — including a well-formed `vendor::name` — is rejected as unknown. Writing under `ultraviolet::...` is the separate reserved-namespace error `E-CNF-0402`.
- **Layout conflicts and bad alignment.** `#layout(packed, align(N))` is a conflicting combination → `E-MOD-2455`. `#layout(packed)` on a non-record → `E-MOD-2454`. `#layout(align(N))` with non-power-of-two `N` → `E-MOD-2453`; with `N` below the natural alignment it is only a warning, `W-MOD-2451`. Remember `#layout(IntType)` is enum-only.
- **Referencing a packed field outside `unsafe` → `E-TYP-2105`.** This fires at the *use site*, not at the declaration. A `#layout(packed)` record is fine to declare anywhere; taking `&field` into it requires an `unsafe` block.
- **`#dynamic` on the wrong place is an error, not a warning.** Putting `#dynamic` directly on a contract clause (`E-CON-0410`), a `type` alias (`E-CON-0411`), or a field (`E-CON-0412`) is ill-formed. A `#dynamic` scope that ends up inserting no runtime checks is the warning `W-CON-0401` — usually a sign the attribute was unnecessary.
- **Malformed `#test` shape errors.** The most common test mistakes each have a precise code: malformed argument `E-TST-0101`; duplicate `name:` `E-TST-0102`; malformed `covers(...)` `E-TST-0103`; invalid procedure shape — generic, missing visibility, missing return type, or extra parameters — `E-TST-0104`; an invalid `TestAuthority` parameter `E-TST-0105`; a missing postcondition `E-TST-0106`; an unknown `covers(...)` ledger reference `E-TST-0107`.
- **Named-argument separator is `:`, not `=`.** Attribute named arguments (`name: "..."`, `library(name: "...", kind: "...")`) use a colon. Writing `name = "..."` does not match `attribute_arg` and is malformed syntax (`E-MOD-2450`). The `#deprecated` message, by contrast, is a bare positional string literal, not a named pair.
- **Unknown `uv test` target → `E-TST-0108`.** A positional argument that is neither an existing host path, nor an assembly name, nor an existing module path is rejected. Only procedures inside the `<Assembly>::Tests` subtree are ever selected — a `#test` outside that subtree will simply never run.

Related chapters: the key/concurrency chapter (memory-order attributes `#relaxed`/`#acquire`/`#release`/`#acqrel`/`#seqcst`, `#stale_ok` staleness semantics), the contracts chapter (postconditions, `@result`, runtime check insertion under `#dynamic`), the refinement-types chapter (runtime refinement checks), the metaprogramming chapter (`#reflect`, `#derive`, `#emit`, `#files`, and the `comptime` forms they attach to), the FFI chapter (`#mangle`, `#library`, `#unwind`, `#export`, `#host_export`, `#ffi_pass_by_value`, and the `#static`/`#dynamic` foreign-contract verification modes), and the standard-library time chapter (`$Time`, `$MonotonicTime`, `MonotonicInstant`, `Duration`, `TimeError`).
