## 27. Foreign Function Interface (FFI)

The Foreign Function Interface (FFI) is the boundary across which Ultraviolet code calls into foreign code compiled to a C-compatible ABI, and across which foreign code calls into Ultraviolet. Every such crossing is an **FFI boundary**, and every boundary is governed by the same concerns: which *types* may legally cross (§27.1), how *control* is transferred and *unwinds* are handled (§27.2, §27.3, §27.8), and which *authority* is allowed to leak across (§27.5). Ultraviolet treats the FFI boundary as a deliberate, narrow, auditable seam — never a convenience escape.

The specification defines the boundary predicate (§23):

```text
FFIBoundary(proc) ⇔
  proc = ExternProcDecl(_, _, _, _, _, _, _, _, _, _, _) ∨
  (proc = ProcedureDecl(_, _, _, _, _, _, _, _, _, _, _) ∧ (ExportAttr(proc) defined ∨ HostExportAttr(proc) defined))
```

That is, a boundary is crossed by a call to an `extern` procedure, or by an invocation from foreign code of a procedure carrying `#export` or `#host_export`.

This chapter mirrors specification §23 (with the `extern` block shell from §11.4). For attribute grammar see also Chapter 9 (Attributes); for `unsafe` see Chapter 18 (Unsafe and Raw Pointers); for contracts see Chapter 16 (Contracts, Invariants, and Predicates); for capabilities and `Context` see Chapter 21 (Capabilities and Context).

> **Style rule (AGENTS.md §FFI Boundaries).** Isolate foreign interaction to dedicated boundary modules. Keep ABI-facing code thin and explicit. Do not let FFI concerns leak into ordinary gameplay, tooling, or simulation code. Prefer safe wrappers that expose project-level types and contracts instead of raw foreign handles or pointers.

### 27.1 `FfiSafe` — Which Types May Cross the Boundary

`FfiSafe` is a **semantic predicate over types**, not surface syntax (§23.1.1, §23.1.3). `FfiSafeType(T)` holds exactly when the runtime representation of `T` is compatible with the platform C ABI. Every type that appears in an `extern` signature, a `#export` signature, or the foreign-visible portion of a `#host_export` signature MUST satisfy `FfiSafeType` (a `()` return is permitted directly without being `FfiSafe`).

#### 27.1.1 The FFI-safe primitive set

The following primitive types are FFI-safe with no further conditions (rule `FfiSafe-Prim`):

```text
FfiPrimTypes = { i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize, f16, f32, f64, char, () }
```

Note carefully what is **absent**: `bool` is **not** FFI-safe. Use a fixed-width integer such as `u8` at the boundary and convert inside a safe wrapper.

#### 27.1.2 Compound FFI-safe types

The predicate is inductive. Each rule below is reproduced from §23.1.4.

**Raw pointers** are always FFI-safe (the pointee need not be):

```text
(FfiSafe-RawPtr)
T = TypeRawPtr(_, _)
──────────────────────────
Γ ⊢ FfiSafeType(T) ⇓ ok
```

**Fixed-size arrays** are FFI-safe when the element type is and the length is a constant:

```text
(FfiSafe-Array)
T = TypeArray(U, n)    Γ ⊢ ConstLen(n) ⇓ _    Γ ⊢ FfiSafeType(U) ⇓ ok
─────────────────────────────────────────────────────────────────────
Γ ⊢ FfiSafeType(T) ⇓ ok
```

**Function-pointer types** are FFI-safe when every parameter type and the return type are:

```text
(FfiSafe-Func)
T = TypeFunc(params, R)    ∀ ⟨_, T_i⟩ ∈ params. Γ ⊢ FfiSafeType(T_i) ⇓ ok    Γ ⊢ FfiSafeType(R) ⇓ ok
────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ FfiSafeType(T) ⇓ ok
```

**Permission-wrapped types** are FFI-safe when their underlying type is:

```text
(FfiSafe-Perm)
T = TypePerm(_, U)    Γ ⊢ FfiSafeType(U) ⇓ ok
──────────────────────────────────────────────
Γ ⊢ FfiSafeType(T) ⇓ ok
```

**Type aliases** are transparent to the predicate. `FfiSafe-Alias` (for a `TypePath` alias) checks the alias body; `FfiSafe-Alias-Apply` (for an applied generic alias) substitutes the type arguments and checks the substituted alias body.

**Records** are FFI-safe only when they carry `#layout(C)`, have no unconstrained generic parameters, have a complete (known) layout, and every field type is itself FFI-safe:

```text
(FfiSafe-Record)
T = TypePath(p)    RecordDecl(p) = R    HasLayoutC(R)    TypeParamsOpt(R.gen_params_opt) = []
Γ ⊢ layout(T) ⇓ _    ∀ f : T_f ∈ Fields(R). Γ ⊢ FfiSafeType(T_f) ⇓ ok
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ FfiSafeType(T) ⇓ ok
```

where `HasLayoutC(D) ⇔ layout(C) appears in D.attrs_opt`.

**Enums** are FFI-safe under the analogous rule `FfiSafe-Enum`: `#layout(C)`, no unconstrained generics, complete layout, and every variant payload field FFI-safe. (Enum payload fields are drawn from `PayloadTypes(v)` over each variant `v`.)

Generic records and enums each have an `-Apply` form (`FfiSafe-Record-Apply`, `FfiSafe-Enum-Apply`) that substitutes arguments before checking, provided the parameters are properly bounded (see §27.1.4).

#### 27.1.3 Prohibited type categories

The following categories MUST NOT satisfy `FfiSafeType`. Any appearance in a boundary signature is the compile-time error `E-TYP-2623` (rule `FfiSafe-Prohibited-Err`). The full predicate is:

```text
ProhibitedFfiType(T) ⇔
  T = TypePrim("bool") ∨ T = TypePtr(_, _) ∨ T = TypeModalState(_, _) ∨ T = ModalRefType(modal_ref) ∨
  T = TypeDynamic(_) ∨ T = TypeOpaque(_) ∨ T = TypeTuple(_) ∨ T = TypeUnion(_) ∨
  T = TypeSlice(_) ∨ T = TypeString(_) ∨ T = TypeBytes(_) ∨
  T = TypeRange(_) ∨ T = TypeRangeInclusive(_) ∨ T = TypeRangeFrom(_) ∨ T = TypeRangeTo(_) ∨
  T = TypeRangeToInclusive(_) ∨ T = TypeRangeFull ∨ T = TypePath(["Context"])
```

Stated plainly (§23.1.4, **Prohibited Categories**), the prohibited categories are:

- `bool`
- Modal types
- Safe pointers `Ptr<T>`
- Dynamic class object types `TypeDynamic(_)` (the `$Class` forms)
- Opaque types
- Tuples
- Unions
- Slices
- String and bytes types
- `Context`
- Range types

To pass any of these across the boundary you must lower it to an FFI-safe representation yourself — e.g. a slice becomes a `*imm T` pointer plus a `usize` length, a string becomes a pointer/length pair or a NUL-terminated `*imm u8`, and a `bool` becomes a `u8`.

#### 27.1.4 Generic bounds and the by-value RAII rule

**Generic bounds.** Any type parameter that appears in a field type or variant payload of a type satisfying `FfiSafeType` MUST be constrained by a class bound that implies `FfiSafe`, such as `<TValue <: FfiSafe>`. Otherwise the type is ill-formed: `E-TYP-2629` (`FfiSafe-Generic-Unbounded-Err`).

**RAII by-value rule.** If a type satisfies *both* `DropType` and `FfiSafeType`, then any by-value appearance of that type in an FFI signature requires the defining `record`/`enum` to carry `#ffi_pass_by_value` (§27.4.6). Without it, the by-value use is `E-TYP-2630`. Formally (§23.1.4):

```text
FfiByValueType(T)     ⇔ StripPerm(T) ∉ {TypeRawPtr(_, _), TypePtr(_, _), TypeFunc(_, _)} ∧ StripPerm(T) ≠ TypePrim("()")
FfiPassByValueAttr(T) ⇔ the record/enum named by T carries #ffi_pass_by_value
FfiByValueOk(T)       ⇔ ¬(DropType(T) ∧ FfiSafeType(T) ⇓ ok ∧ FfiByValueType(T)) ∨ FfiPassByValueAttr(StripPerm(T))
```

#### 27.1.5 Worked example — an FFI-safe record and a generic FFI-safe record

```ultraviolet
//! Boundary-facing C-layout value types.

/// A 2D point passed by value across the C ABI.
#layout(C)
public record Point {
    public x: f64,
    public y: f64
}

/// A pair generic over any FFI-safe element. The `<TValue <: FfiSafe>` bound is
/// mandatory because `TValue` appears in the field types.
#layout(C)
public record Pair<TValue <: FfiSafe> {
    public first: TValue,
    public second: TValue
}
```

Here `Point` and `Pair<i32>` both satisfy `FfiSafeType`. Omitting `#layout(C)` would raise `E-TYP-2624`; omitting the `<TValue <: FfiSafe>` bound on `Pair` would raise `E-TYP-2629`.

#### 27.1.6 Diagnostics (§23.1.7)

| Code | Condition |
| :--- | :--- |
| `E-TYP-2623` | Prohibited type category in `FfiSafeType` (`FfiSafe-Prohibited-Err`) |
| `E-TYP-2624` | `FfiSafeType` record without `#layout(C)` (`FfiSafe-Record-LayoutC-Err`) |
| `E-TYP-2625` | `FfiSafeType` enum without `#layout(C)` (`FfiSafe-Enum-LayoutC-Err`) |
| `E-TYP-2626` | `FfiSafeType` record has a non-`FfiSafeType` field (`FfiSafe-Record-Field-Err`) |
| `E-TYP-2627` | `FfiSafeType` enum has a non-`FfiSafeType` payload field (`FfiSafe-Enum-Field-Err`) |
| `E-TYP-2628` | `FfiSafeType` requires a complete layout (`FfiSafe-Incomplete-Err`) |
| `E-TYP-2629` | Generic `FfiSafeType` with an unconstrained parameter (`FfiSafe-Generic-Unbounded-Err`) |
| `E-TYP-2630` | By-value FFI use of a `DropType` without `#ffi_pass_by_value` |

### 27.2 Extern Procedures — Calling Foreign Code

An **extern procedure** is a declaration whose implementation is supplied by foreign code. You declare it (with no body) inside an `extern` block (§27.7), then call it from an `unsafe` block.

#### 27.2.1 Syntax

```ebnf
extern_procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? foreign_contract_clause_list? terminator
```

The AST form is (§23.2.3):

```text
ExternProcDecl = ⟨attrs_opt, vis, name, gen_params_opt, params, return_type_opt, contract_opt, foreign_contracts_opt, span, doc⟩
```

Unlike an ordinary `procedure_decl`, an extern procedure ends in a `terminator` (a newline or `;`) rather than a `block_expr` — it has no body.

#### 27.2.2 ABI strings

The ABI is selected by the `extern` block's ABI string (§27.7). The valid set and their platform constraints are (§23.2.4):

```text
ExternAbiSet = { "C", "C-unwind", "system", "stdcall", "fastcall", "vectorcall" }
AbiProfileOk("C", profile)            (always)
AbiProfileOk("C-unwind", profile)     (always)
AbiProfileOk("system", profile)       (always)
AbiProfileOk("stdcall", profile)    ⇔ profile = x86_64-win64
AbiProfileOk("fastcall", profile)   ⇔ profile = x86_64-win64
AbiProfileOk("vectorcall", profile) ⇔ profile = x86_64-win64
```

An ABI string outside `ExternAbiSet`, or one not valid for the selected target, is rejected: `E-SYS-3352` (`ExternAbi-Unknown-Err`, §23.8).

#### 27.2.3 Signature requirements

```text
ExternSigOk(params, ret_opt) ⇔
  R = ProcReturn(ret_opt) ∧
  (R = TypePrim("()") ∨ Γ ⊢ FfiSafeType(R) ⇓ ok) ∧
  (∀ T ∈ ExternParamTypes(params). Γ ⊢ FfiSafeType(T) ⇓ ok) ∧
  (∀ T ∈ ExternParamTypes(params). FfiByValueOk(T)) ∧
  FfiByValueOk(R)
```

Three additional FFI constraints apply specifically to `extern` signatures (§23.2.4):

1. **Closure types MUST NOT appear** in `extern` signatures.
2. Only *sparse function-pointer types* are FFI-safe there (`SparseFuncType(T) ⇔ T = TypeFunc(_, _)`).
3. Sparse function-pointer types in `extern` signatures MUST NOT have generic type parameters.

A generic parameter in an `extern` procedure signature is a compile-time error: `E-TYP-2306`.

#### 27.2.4 Call safety

**Calls to extern procedures MUST appear within an `unsafe` block** (`unsafe_block ::= "unsafe" block_expr`). A call outside `unsafe` is `E-TYP-2106`.

#### 27.2.5 Dynamic semantics

Calling an extern procedure transfers control to foreign code (§23.2.5). If `UnwindMode(proc) = "catch"`, a foreign unwind that reaches the boundary is converted to an Ultraviolet panic (§27.8). If `UnwindMode(proc) = "abort"` (the default), any unwind that attempts to cross the boundary aborts the program.

#### 27.2.6 Worked example — declaring and calling a C function

```ultraviolet
//! Thin boundary over the C standard library, wrapped in a safe API.

#library(name: "c")
extern "C" {
    /// Foreign `abs` from libc. FFI-safe by-value `i32` in and out.
    public procedure abs(value: i32) -> i32

    /// Foreign `memcpy`. Raw pointers and `usize` are all FFI-safe.
    public procedure memcpy(dest: *mut u8, src: *imm u8, count: usize) -> *mut u8
}

/// Safe wrapper: total, no `unsafe` visible to callers, re-establishes the
/// project invariant that the result is the magnitude of `value`.
public procedure absoluteValue(value: i32) -> i32 {
    let result: i32 = unsafe { abs(value) }
    return result
}
```

#### 27.2.7 Diagnostics (§23.2.7)

| Code | Condition |
| :--- | :--- |
| `E-TYP-2306` | Generic parameter in `extern` procedure signature |
| `E-TYP-2106` | Call to `extern` procedure outside `unsafe` |

Type-admissibility and by-value diagnostics are owned by §27.1.6; unsupported-ABI rejection by §27.2.2 / §23.8.

### 27.3 Foreign-Callable Procedure Exports — Exposing Ultraviolet to Foreign Callers

Ultraviolet exposes a procedure to foreign callers in one of two modes. A **raw exported procedure** carries `#export("abi")` and presents a literal C-ABI signature. A **hosted export** carries `#host_export("abi")` and presents a derived foreign signature plus an opaque session handle, used by library assemblies that need a managed `Context`. The two modes MUST NOT be mixed in one assembly. This mirrors specification §23.3: raw exported procedures are the leaf feature in §23.3.1, and hosted exports are the leaf feature in §23.3.2.

#### 27.3.1 Raw exported procedures

A procedure becomes a raw exported procedure when its attribute list contains `#export("abi")`. It remains an ordinary `procedure_decl` (parsed by the normal procedure parser) with a `ProcedureDecl(...)` AST node; there is no dedicated raw-export node (§§23.3.1.1–23.3.1.3). The classification is simply: `ExportAttr(proc)` is defined.

The signature obligation is `ExportSig-Ok` (§23.3.1.4):

```text
(ExportSig-Ok)
proc = ProcedureDecl(_, vis, _, _, _, params, ret_opt, _, _, _, _)    vis = public
ExportAttr(proc) = ⟨abi, _⟩    abi ∈ ExternAbiSet    AbiProfileOk(abi, SelectedTargetProfile)
R = ProcReturn(ret_opt)    (R = TypePrim("()") ∨ Γ ⊢ FfiSafeType(R) ⇓ ok)
(∀ T ∈ ExportParamTypes(params). Γ ⊢ FfiSafeType(T) ⇓ ok)
(∀ T ∈ ExportParamTypes(params). FfiByValueOk(T))    FfiByValueOk(R)
(UnwindMode(proc) ≠ catch ∨ ZeroableType(R))
─────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ExportSigOk(proc) ⇓ ok
```

Key requirements:

- The procedure MUST be `public` (`E-SYS-3353` otherwise).
- Every parameter and the return type must be `FfiSafe` (or `()` for the return), and by-value RAII rules apply.
- If `UnwindMode(proc) = "catch"`, the return type `R` MUST be **zeroable** (`ZeroableType(R)`), because a caught panic returns `ZeroValue(R)` (`E-TYP-2631` otherwise). A zeroable type is one for which the all-zero bit pattern is a valid, unique value:

```text
ZeroBits(T)    = [0x00, …, 0x00] where |ZeroBits(T)| = sizeof(T)
ZeroValue(T)   = v ⇔ ValueBits(T, v) = ZeroBits(T) ∧ ∀ v'. (ValueBits(T, v') = ZeroBits(T) ⇒ v' = v)
ZeroableType(T) ⇔ ∃ v. ZeroValue(T) = v
```

`ZeroValue(R)` is the boundary's failure result: when a panic is caught at the export boundary, the procedure returns the all-zero value of its return type rather than propagating the unwind (§27.8).

##### Worked example — a raw export

```ultraviolet
//! Foreign-callable surface of this static library.

/// Adds two C `i32`s. `#export("C-unwind")` makes this callable from C as `add`.
/// `#unwind("catch")` requires the `C-unwind` ABI and a zeroable return; on
/// panic the boundary returns `0`.
#export("C-unwind")
#unwind("catch")
public procedure add(left: i32, right: i32) -> i32 {
    return left + right
}
```

#### 27.3.2 Hosted exports

A **hosted export** carries `#host_export("abi")`. It is *not* a raw FFI signature. Instead the foreign-visible signature is derived from the source procedure: one leading `usize` **session-handle** parameter, followed by every source parameter *except the first*. The first source parameter is a projected `Context` bundle that is reconstructed inside the boundary thunk from session-owned state; it never appears in the foreign ABI.

The classification and obligation is `HostExportSig-Ok` (§23.3.2.4), with its load-bearing premises shown:

```text
(HostExportSig-Ok)  [abridged to its load-bearing premises]
Project(Γ) = P    proc = ProcedureDecl(_, vis, _, gen_params_opt, _, params, ret_opt, _, _, _, _)    vis = public
HostExportAttr(proc) = ⟨abi, _⟩    TypeParamsOpt(gen_params_opt) = []    ¬ MixedForeignExportModes(P)
Library(P)    params = [⟨⊥, _, T_ctx⟩] ++ params_vis    HostedContextBundleType(StripPerm(T_ctx))
abi ∈ ExternAbiSet    AbiProfileOk(abi, SelectedTargetProfile)    R = ProcReturn(ret_opt)
(R = TypePrim("()") ∨ Γ ⊢ FfiSafeType(R) ⇓ ok)
(∀ T ∈ HostVisibleParamTypes(proc). Γ ⊢ FfiSafeType(T) ⇓ ok)
(∀ T ∈ HostVisibleParamTypes(proc). CapInType(T) = ∅)    CapInType(R) = ∅
(∀ T ∈ HostVisibleParamTypes(proc). FfiByValueOk(T))    FfiByValueOk(R)
(UnwindMode(proc) ≠ catch ∨ ZeroableType(R))
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ HostExportSigOk(proc) ⇓ ok
```

Requirements specific to hosted exports:

1. The procedure MUST be `public` and **non-generic** (`E-TYP-2634`, `HostExport-Generic-Err`, for a generic hosted export).
2. The owning assembly MUST be a **library** assembly (`assembly.kind = "library"`); otherwise `E-SYS-3357` / `HostExport-Library-Err`.
3. `#host_export` and `#export` MUST NOT both appear in the same assembly (`E-SYS-3358` / `HostExport-MixedMode-Err`).
4. The **first source parameter** MUST be a projected `Context` bundle (`HostedContextBundleType`) — a context-bundle `record` type that is *not* raw `Context`. A missing or non-bundle leading parameter is `E-TYP-2632` (`HostExport-Context-Err`); a leading parameter that normalizes to raw `Context` rather than a projected bundle is `E-TYP-2636` (`HostExport-Context-Raw-Err`).
5. The leading bundle parameter MUST NOT use `move` (`E-TYP-2633` / `HostExport-Context-Move-Err`).
6. The *visible* parameters (everything after the first) and the return MUST be `FfiSafe` *and* capability-free (`CapInType(...) = ∅`).
7. If `UnwindMode = "catch"`, `R` MUST be zeroable (`E-TYP-2635`).

A **context bundle** is an ordinary `record` whose every field is either a recognized context-bundle field or a nested context bundle (§13). A recognized field has one of the reserved names below, and its type (after stripping permissions) must be exactly the matching dynamic class object type:

```text
ContextBundleFieldType(`io`)      = $IO            ContextBundleFieldType(`reactor`) = $Reactor
ContextBundleFieldType(`net`)     = $Network       ContextBundleFieldType(`time`)    = $Time
ContextBundleFieldType(`heap`)    = $HeapAllocator ContextBundleFieldType(`cpu`)     = $ExecutionDomain
ContextBundleFieldType(`sys`)     = $System        ContextBundleFieldType(`gpu`)     = $ExecutionDomain
                                                   ContextBundleFieldType(`inline`)  = $ExecutionDomain
```

The boundary reconstructs the first argument via `ContextBundleBuild` from session state, so foreign code never supplies capability values. The bundle parameter is *not* part of the foreign-visible ABI, so it does **not** need to be `FfiSafe` (indeed it cannot be — `$Class` dynamic types are prohibited by `FfiSafeType`).

The foreign-visible thunk ABI prepends `HostSessionAbiParam = ⟨move, __ultraviolet_session, usize⟩`, then the visible parameters. The backend additionally emits the hosted-library lifecycle exports `__ultraviolet_host_abi_version : () -> u32`, `__ultraviolet_host_session_create : () -> usize`, and `__ultraviolet_host_session_destroy : (usize) -> u32` once per linked image (§23.3.2.6). The handle value `0` is always invalid.

##### Worked example — a hosted export

```ultraviolet
//! Hosted-library export. Built as an assembly with `assembly.kind = "library"`.

/// A narrow projected context bundle carrying just the capabilities this
/// boundary needs. Field names and types are the reserved context-bundle forms
/// (`time` : `$Time`). It is reconstructed from the session, never passed by C.
public record RenderContext {
    public time: $Time
}

/// Foreign-visible signature is `(usize session, u32 frame) -> u32`.
/// The leading `RenderContext` bundle is reconstructed inside the thunk and is
/// NOT part of the C ABI; it must not use `move`. The visible parameter `frame`
/// and the return `u32` are FFI-safe and capability-free.
#host_export("C-unwind")
#unwind("catch")
public procedure stepFrame(context: RenderContext, frame: u32) -> u32 {
    return frame + 1
}
```

#### 27.3.3 Diagnostics (§23.3.1.7, §23.3.2.7)

| Code | Condition |
| :--- | :--- |
| `E-SYS-3353` | `#export` requires `public` visibility |
| `E-TYP-2631` | `#export` catch requires a zeroable return type |
| `E-TYP-2632` | `#host_export` requires a leading `Context` bundle parameter (`HostExport-Context-Err`) |
| `E-TYP-2633` | `#host_export` leading bundle parameter MUST NOT use `move` (`HostExport-Context-Move-Err`) |
| `E-TYP-2634` | Generic `#host_export` procedure (`HostExport-Generic-Err`) |
| `E-TYP-2635` | `#host_export` catch requires a zeroable return type |
| `E-TYP-2636` | `#host_export` MUST use a projected `Context` bundle, not raw `Context` (`HostExport-Context-Raw-Err`) |

### 27.4 FFI Attributes

#### 27.4.1 Syntax (§23.4.1)

```ebnf
mangle_attribute            ::= "#" "mangle" "(" mangle_mode ")"
mangle_mode                 ::= "none" | string_literal

library_attribute           ::= "#" "library" "(" library_args ")"
library_args                ::= "name" ":" string_literal ("," "kind" ":" string_literal)?

unwind_attribute            ::= "#" "unwind" "(" unwind_mode ")"
unwind_mode                 ::= string_literal

export_attribute            ::= "#" "export" "(" string_literal ")"
host_export_attribute       ::= "#" "host_export" "(" string_literal ")"

ffi_pass_by_value_attribute ::= "#" "ffi_pass_by_value"
```

Targets (§23.4.3):

| Attribute | Valid targets |
| :--- | :--- |
| `mangle` | `Procedure` |
| `library` | `ExternBlock` |
| `unwind` | `Procedure` |
| `export` | `Procedure` |
| `host_export` | `Procedure` |
| `ffi_pass_by_value` | `Record`, `Enum` |

#### 27.4.2 `#mangle`

Valid only on extern procedures, raw exported procedures, and hosted exports (§23.4.4.1).

- `#mangle(none)` sets the link name to the declaration identifier, unmangled.
- `#mangle("name")` sets the link name to the exact string given; the string MUST be non-empty and valid for the target linker.
- On a non-FFI procedure, `#mangle(...)` is ill-formed (`E-SYS-3340`); `#mangle(none)` on a non-exportable procedure is `E-SYS-3350`; an invalid mode argument is `E-SYS-3341`.
- `#mangle(none)` together with `#export("C")` is redundant and SHOULD warn (`W-SYS-3350`).

```ultraviolet
#library(name: "crypto")
extern "C" {
    /// Bind to the foreign symbol `SHA256_Init` exactly, with no mangling.
    #mangle("SHA256_Init")
    public procedure sha256Init(context: *mut u8) -> i32
}
```

#### 27.4.3 `#library`

Valid only on `extern` blocks (§23.4.4.2). The `name` argument is the library name without platform prefix or suffix. The optional `kind` selects the link kind (default `"dylib"`):

| Kind | Meaning |
| :--- | :--- |
| `"dylib"` | Dynamic library (default) |
| `"static"` | Static library |
| `"framework"` | macOS framework |
| `"raw-dylib"` | Windows named DLL import |

Resolution maps the name per target, e.g. `dylib` → `lib<name>.so` (sysv/aapcs64), `lib<name>.dylib` (aarch64-darwin), `<name>.dll` (win64); `static` → `lib<name>.a` (sysv/aapcs64/darwin), `<name>.lib` (win64); `raw-dylib` → `<name>.dll` (win64 only). `framework` is never supported by this revision (`LibraryKindSupported("framework", profile) ⇔ false`), and `raw-dylib` is supported only on `x86_64-win64`. An unsupported kind is `E-SYS-3346`, a `#library` outside an `extern` block is `E-SYS-3345`, and a library not found at link time is `E-SYS-3347`. This attribute governs *foreign-library resolution only* and is independent of the manifest key `assembly.link_kind` (§3.2).

#### 27.4.4 `#unwind`

Valid only on extern procedures, raw exported procedures, and hosted exports (§23.4.4.3).

| Mode | Behavior |
| :--- | :--- |
| `"abort"` | Any panic or foreign unwind that would cross the boundary aborts. |
| `"catch"` | Unwinding is caught at the boundary; imported procedures convert foreign unwinds to Ultraviolet panics. |

If `#unwind` is omitted, `"abort"` is assumed. `#unwind("abort")` is redundant and SHOULD warn (`W-SYS-3355`).

**Catch ABI requirement.** If `UnwindMode(proc) = "catch"`, the boundary ABI MUST be `"C-unwind"` — for extern procedures the extern ABI must be `"C-unwind"` (`ExternAbiName(ExternAbiOf(proc)) = "C-unwind"`); for `#export` the attribute's ABI string must be `"C-unwind"`; for `#host_export` the attribute's ABI string must be `"C-unwind"`.

Two or more `#unwind` attributes is `E-FFI-0350`; an unknown mode string is `E-SYS-3355`; `#unwind` on a non-FFI procedure is `E-SYS-3356`.

#### 27.4.5 `#export` and `#host_export`

`#export` (raw) and `#host_export` (hosted) each: are valid only on `procedure` declarations; require `public`; take an ABI string selecting the foreign calling convention; imply external linkage (for `#host_export`, through the generated thunk — the source body keeps ordinary visibility-based linkage and is not itself the foreign entrypoint); and select link names via `LinkName` and `#mangle`. `#host_export` additionally requires `assembly.kind = "library"`, and `#host_export` and `#export` MUST NOT appear in the same assembly (§23.4.4.4–§23.4.4.5).

#### 27.4.6 `#ffi_pass_by_value`

Marks a `record` or `enum` that satisfies *both* `DropType` and `FfiSafeType` as eligible for by-value passing across the boundary. Without it, by-value FFI use of such a type is ill-formed (§27.1.4, `E-TYP-2630`).

```ultraviolet
//! A boundary handle that owns a resource (Drop) yet is C-layout (FfiSafe),
//! and is explicitly authorized to pass by value across the boundary.

#layout(C)
#ffi_pass_by_value
public record FileHandle {
    public fd: i32
}
```

#### 27.4.7 Diagnostics (§23.4.7)

| Code | Condition |
| :--- | :--- |
| `E-SYS-3340` | `#mangle(...)` on a non-FFI procedure |
| `E-SYS-3341` | Invalid `#mangle(mode)` argument |
| `E-SYS-3342` | Duplicate symbol name in compilation unit |
| `E-SYS-3345` | `#library` outside an `extern` block |
| `E-SYS-3346` | Unknown or unsupported library kind |
| `E-SYS-3347` | Library not found (link-time) |
| `E-SYS-3350` | `#mangle(none)` on a non-exportable procedure |
| `E-SYS-3351` | Conflicting explicit mangling directives |
| `E-SYS-3355` | Unknown unwind mode (`UnwindMode-Invalid-Err`) |
| `E-SYS-3356` | `#unwind` on a non-FFI procedure |
| `E-SYS-3357` | `#host_export` requires `assembly.kind = "library"` (`HostExport-Library-Err`) |
| `E-SYS-3358` | `#host_export` and `#export` mixed in one assembly (`HostExport-MixedMode-Err`) |
| `E-FFI-0350` | Multiple `#unwind` attributes |
| `W-SYS-3350` | `#mangle(none)` with `#export("C")` (redundant) |
| `W-SYS-3355` | `#unwind("abort")` (redundant) |

### 27.5 Capability Isolation Across the FFI Boundary

**Foreign code MUST NOT receive or return capability-bearing values** (§23.5.4). This is the structural guarantee that authority cannot leak out of (or be forged into) the safe Ultraviolet world through the FFI seam.

Two rules enforce it:

1. Any raw FFI signature or hosted-export *visible* signature that contains `Context`, a capability class, or a dynamic class object is ill-formed. (`Context` is already prohibited by `FfiSafeType` per §27.1.3; the hosted-export rule additionally requires `CapInType = ∅` on every visible parameter and the return.)
2. A **raw pointer derived from region-local storage MUST NOT cross an FFI boundary.** Passing such a pointer as an argument, or returning it from an exported/hosted procedure, is rejected:

```text
RegionLocalProv(π) ⇔ ∃ tag. π = π_Region(tag)
RawPtrType(T)      ⇔ T = TypeRawPtr(_, _)
FFICall(Call(callee, args)) ⇔ CalleeProc(callee) = proc ∧ FFIBoundary(proc)
```

```text
(FFI-Arg-RegionLocalRawPtr-Err)
FFICall(Call(callee, args))    ∃ ⟨_, arg, _⟩ ∈ args. Γ; Ω ⊢ arg ⇓ π ∧ RegionLocalProv(π) ∧ RawPtrType(ExprType(arg))
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; Ω ⊢ Call(callee, args) ⇑

(FFI-Return-RegionLocalRawPtr-Err)
CurrentProcedure(Γ) = proc    (ExportAttr(proc) defined ∨ HostExportAttr(proc) defined)    Γ; Ω ⊢ e ⇓ π    RegionLocalProv(π)    RawPtrType(ExprType(e))
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; Ω ⊢ ReturnStmt(e) ⇑
```

A region-local raw pointer escaping an FFI boundary is `E-SYS-3360`. Capability-type violations other than region-local pointer escape are caught by the §27.1 type-admissibility checks (§23.5.7).

The practical consequence: hand foreign code only pointers into storage that outlives the call (heap, static, or caller-provided buffers), never a pointer into a region that the boundary call could outlive.

### 27.6 Foreign Contracts

Foreign contracts let an extern declaration state, in machine-checkable form, what the *caller* must guarantee before the call (`@foreign_assumes`) and what the *foreign code* guarantees on return (`@foreign_ensures`). They attach to the extern declaration's `foreign_contracts_opt`.

#### 27.6.1 Syntax (§23.6.1)

```ebnf
ffi_verification_attr        ::= "#" ffi_verification_mode
ffi_verification_mode        ::= "static" | "dynamic"

foreign_contract             ::= "|:" decorated_identifier("@", "foreign_assumes") "(" predicate_expr ")"
                               | "|:" decorated_identifier("@", "foreign_ensures") "(" ensures_predicate ")"
foreign_contract_clause_list ::= foreign_contract+
ensures_predicate            ::= predicate_expr
                               | decorated_identifier("@", "error") ":" predicate_expr
                               | decorated_identifier("@", "null_result") ":" predicate_expr
```

The `@...` forms are decorated identifiers: `Operator("@")` followed by the
named identifier, not combined lexer tokens.

#### 27.6.2 Foreign preconditions — `@foreign_assumes`

A precondition the caller must satisfy. Predicates MAY reference parameter names from the signature, literal constants, pure functions and operators, and fields of parameter values. They MUST NOT reference global mutable state, values not in scope at the call site, or effectful operations (§23.6.4.1).

Verification mode is chosen by `#static` (default) or `#dynamic`:

| Mode | Behavior |
| :--- | :--- |
| `#static` | Caller must prove the predicate at compile time (`StaticProof`, §15.8). |
| `#dynamic` | A runtime check (`ContractCheck(P, ForeignPre, s, ρ_emptyset)`) is inserted immediately before the foreign call. |

A statically unprovable precondition is `E-SEM-2850`; a failed `#dynamic` precondition panics at runtime (`P-SEM-2860`).

#### 27.6.3 Foreign postconditions — `@foreign_ensures`

Conditions the foreign code guarantees on return. Postcondition predicates MAY reference `@result` (the return value), parameter names (for output parameters), `@error` (predicates that hold on failure), and `@null_result` (predicates that hold when the result is null) (§23.6.4.2).

Classification (let `U` be the unconditional predicates, `E` the `@error` predicates, `N` the `@null_result` predicates):

```text
ErrCond     = ⋀_{P ∈ E} P     if E ≠ ∅,   else false
NullCond    = (@result == null)
SuccessCond = ¬ ErrCond
```

The foreign call is classified as an error iff `ErrCond` holds; otherwise it is success. The obligations are: for each `P ∈ U`, `SuccessCond ⇒ P`; for each `P ∈ E`, `ErrCond ⇒ P`; for each `P ∈ N`, `NullCond ⇒ P`.

Well-formedness rules:

- `@null_result` is well-formed **only** when the return type is a nullable pointer type. The nullable pointer types are `Ptr<T>@Null`, `*imm T`, or `*mut T` (`NullableFfiResult(T) ⇔ T = TypePtr(_, @Null) ∨ T = TypeRawPtr(imm, _) ∨ T = TypeRawPtr(mut, _)`). Otherwise `E-SEM-2856`.
- `@error` is well-formed only when the return type is not `()`. Using `@error` on a void-returning foreign procedure is `E-SEM-2855`.

Verification:

| Mode | Behavior |
| :--- | :--- |
| `#static` | Postconditions become assumptions for downstream proofs, gated by `SuccessCond` / `ErrCond`. |
| `#dynamic` | Runtime assertions after the foreign call returns (`ErrCond` and `NullCond` evaluated left-to-right). |

A failed `#dynamic` postcondition panics (`P-SEM-2861`) with payload `ContractViolation(ForeignPost, P, s)` at the call site.

#### 27.6.4 Worked example — a contract-bearing extern

```ultraviolet
//! Foreign allocator bound with caller obligations and return guarantees.

#library(name: "c")
extern "C" {
    /// Caller must request a positive size. The foreign result is null exactly
    /// when allocation fails. Because the return type is `*mut u8`, the
    /// `@null_result` clause is well-formed.
    #dynamic
    public procedure malloc(size: usize) -> *mut u8
        |: @foreign_assumes(size > 0)
        |: @foreign_ensures(@null_result: @result == null)
}
```

Here `#dynamic` inserts a runtime check that `size > 0` before the call and a runtime check of the null-result implication after it.

#### 27.6.5 Diagnostics (§23.6.7)

| Code | Condition |
| :--- | :--- |
| `E-SEM-2850` | Cannot prove `@foreign_assumes` predicate |
| `E-SEM-2851` | Invalid predicate in foreign contract |
| `E-SEM-2852` | Predicate references an out-of-scope value |
| `E-SEM-2853` | Invalid predicate in `@foreign_ensures` |
| `E-SEM-2854` | `@result` used in a non-return context |
| `E-SEM-2855` | `@error` predicate on a void-returning procedure |
| `E-SEM-2856` | `@null_result` predicate on a non-nullable return type |
| `P-SEM-2860` | Foreign precondition failed at runtime |
| `P-SEM-2861` | Foreign postcondition failed at runtime |

### 27.7 The `extern` Block Shell (§11.4)

Extern procedures live inside an `extern` block, which supplies the shared ABI and library-resolution context.

#### 27.7.1 Syntax (§11.4.1)

```ebnf
extern_block ::= attribute_list? visibility? "extern" extern_abi? "{" extern_item* "}"
extern_abi   ::= string_literal | identifier
extern_item  ::= extern_procedure_decl
```

The Appendix B form spells the ABI as `abi_string ::= string_literal` and the item as `foreign_procedure`; both are the same construct as `extern_abi` (string form) and `extern_procedure_decl`.

The AST form is `ExternBlock = ⟨attrs_opt, vis, abi_opt, items, span, doc⟩`, with `abi_opt ∈ {⊥} ∪ ExternAbi` and `ExternAbi ∈ {StringAbi, IdentAbi}` (§11.4.3).

#### 27.7.2 Semantics

The block's ABI is validated by `ExternAbiOk` (§23.2.4): a block whose ABI is unknown or unsupported on the target is `E-SYS-3352` (`ExternAbi-Unknown-Err`, rule `WF-ExternBlock` / `ExternAbi-Unknown-Err` in §11.4.4). The block contributes ABI and library-resolution context to every contained foreign procedure; it introduces no runtime mechanism of its own (§11.4.5). Contained-procedure signature diagnostics belong to the rules of §27.1–§27.6.

#### 27.7.3 Worked example — a complete extern block

```ultraviolet
//! A self-contained boundary module over a math library, wrapped safely.

#library(name: "fastmath", kind: "static")
extern "C" {
    /// Foreign fast inverse square root.
    public procedure fastInvSqrt(value: f32) -> f32

    /// Foreign clamp into [low, high].
    public procedure clampF32(value: f32, low: f32, high: f32) -> f32
}

/// Safe project-level API. Callers never see `unsafe` or the foreign symbols.
public procedure normalizeGain(raw_gain: f32) -> f32 {
    let clamped: f32 = unsafe { clampF32(raw_gain, 0.0, 1.0) }
    return clamped
}
```

### 27.8 Boundary Unwinding and Panic Safety

The `#unwind` attribute (§27.4.4) is the only surface syntax owned by this area. The boundary unwind policy is `UnwindMode(proc) ∈ { abort, catch }`, with `abort` the default when `#unwind` is absent (§23.7.3):

```text
UnwindMode(proc) = m       ⇔ UnwindAttr(proc) = m
UnwindMode(proc) = abort   ⇔ UnwindAttr(proc) undefined
UnwindAttr(proc) = m       ⇔ ∃ a ∈ AttrByName(proc, "unwind"). a.args = [StringLiteral(m)] ∧ m ∈ {abort, catch}
```

The mode is determined by (§23.7.4):

```text
DetermineUnwindMode(proc) =
  let attrs = AttrByName(proc, "unwind")
  match attrs {
    []  → abort
    [a] → ParseUnwindArg(a)
    _   → Emit(E-FFI-0350)
  }

ParseUnwindArg(a) =
  match a.args {
    [StringLiteral("abort")] → abort
    [StringLiteral("catch")] → catch
    _                        → Emit(E-SYS-3355)
  }
```

#### 27.8.1 Boundary effects (§23.7.5)

1. If an Ultraviolet panic or a foreign unwind attempts to cross an FFI boundary whose `UnwindMode(proc) = abort`, the program **MUST abort**.
2. If `UnwindMode(proc) = catch`:
   - imported (extern) procedures **convert foreign unwinds to Ultraviolet panics**;
   - raw exported procedures **return `ZeroValue(R)`**;
   - hosted exports **return `ZeroValue(R)`**.

Recall the catch-mode preconditions enforced statically: the boundary ABI MUST be `"C-unwind"` (§27.4.4), and for exports/hosted exports the return type `R` MUST be zeroable (§27.3). For hosted exports, a boundary failure that occurs *before or during* invocation — including an invalid, non-live, or busy session handle — likewise returns `ZeroValue(R)` under catch mode, and otherwise terminates the boundary call as `Abort` (§23.3.2.5).

#### 27.8.2 Code generation (§23.7.6)

| Mode | Import (calling extern) | Export / Hosted export (called from foreign) |
| :--- | :--- | :--- |
| `abort` | Install a landing pad that aborts on foreign unwind. | Install a frame that aborts if an Ultraviolet panic escapes. |
| `catch` | Install a landing pad that converts to an Ultraviolet panic. | Install a frame that catches the unwind and returns the boundary zero. |

`#unwind` placement and argument-validation diagnostics are owned by §27.4.7.

#### 27.8.3 Worked example — a panic-safe export

```ultraviolet
//! A foreign-callable entrypoint that must never let a panic escape into C.

#library(name: "reader")
extern "C-unwind" {
    /// Foreign reader; foreign unwinds are converted to Ultraviolet panics
    /// because this block's procedures are called under `catch` import frames.
    #unwind("catch")
    public procedure readU32(data: *imm u8) -> u32
}

/// `catch` requires the `C-unwind` ABI and a zeroable return (`u32`). If the
/// body panics, the boundary returns `0` instead of unwinding into foreign
/// frames. The `if` body uses an explicit block, as required by the grammar.
#export("C-unwind")
#unwind("catch")
public procedure parseHeader(data: *imm u8, length: usize) -> u32 {
    if length < 4 {
        return 0
    }
    return unsafe { readU32(data) }
}
```

### Idioms & Best Practices

- **Keep the boundary thin and isolated.** Put `extern` blocks, `#export`, and `#host_export` declarations in dedicated boundary modules. Never let raw pointers, foreign handles, or `unsafe` leak into gameplay, tooling, or simulation code (AGENTS.md §FFI Boundaries).
- **Always wrap FFI in a safe API.** Expose project-level types and contracts, not foreign symbols. A caller of `normalizeGain` or `absoluteValue` above should never see `unsafe` or a raw pointer. Re-establish project invariants inside the wrapper (AGENTS.md §`unsafe`).
- **Keep `unsafe` blocks minimal.** The required `unsafe` around an extern call should contain *only* the call. Document ownership, lifetime, thread affinity, and caller obligations at every unsafe boundary (AGENTS.md §`unsafe`).
- **Lower prohibited types explicitly.** Convert `bool` ⇄ `u8`, slices ⇄ `(*imm T, usize)`, and strings ⇄ pointer/length (or NUL-terminated `*imm u8`) in the wrapper. Do not attempt to pass a `bool`, slice, tuple, string, range, modal type, dynamic object, or `Context` directly — they are not `FfiSafe`.
- **Make C-ABI types `#layout(C)` and document them.** Every record/enum crossing the boundary needs `#layout(C)`. Bound every generic parameter that appears in such a type with `<T <: FfiSafe>` or a subclass of `FfiSafe`.
- **Prefer `catch` only when you can recover.** `catch` forces the `C-unwind` ABI and a zeroable return; design the return type so that `ZeroValue(R)` is a meaningful failure sentinel (e.g. `0` length, a null pointer). When abort-on-panic is the right policy, accept the `abort` default and omit `#unwind`.
- **Use foreign contracts to encode caller and callee obligations.** Express positivity, non-null, length, and error/null-result guarantees with `@foreign_assumes` and `@foreign_ensures` rather than ad-hoc runtime checks. FFI wrappers should be especially strict about contracts (AGENTS.md §Contracts Are Mandatory Where Expressible).
- **Use hosted exports for capability-managed libraries.** When foreign callers need access to project capabilities, prefer `#host_export` with a *narrow projected* `Context` bundle over hand-rolled global state. Pass only the capabilities the boundary actually uses (AGENTS.md §Capability Passing).
- **Preserve foreign casing in link names.** Foreign ABI names may keep their external casing via `#mangle("Exact_Foreign_Name")` even where it breaks local naming conventions (AGENTS.md §Naming Exceptions).

### Pitfalls & Diagnostics

- **`bool` at the boundary → `E-TYP-2623`.** `bool` is *not* in `FfiPrimTypes`. Use `u8`. The same error fires for tuples, unions, slices, strings/bytes, ranges, modal types, `Ptr<T>`, dynamic objects (`$Class`), opaque types, and `Context`.
- **Missing `#layout(C)` → `E-TYP-2624` / `E-TYP-2625`.** A record/enum used at the boundary without `#layout(C)` is not `FfiSafe`.
- **Unbounded generic in an FFI-safe aggregate → `E-TYP-2629`.** Add a `<TValue <: FfiSafe>` bound for every parameter used in a field or payload.
- **By-value `Drop` type without `#ffi_pass_by_value` → `E-TYP-2630`.** Either pass it behind a raw pointer, or mark the defining record/enum `#ffi_pass_by_value`.
- **Calling an extern outside `unsafe` → `E-TYP-2106`.** Wrap the call. Conversely, a generic parameter on the extern signature itself is `E-TYP-2306`.
- **`catch` without `C-unwind` or with a non-zeroable return.** `#unwind("catch")` demands the `C-unwind` ABI; an export/hosted export with a non-zeroable `R` under catch is `E-TYP-2631` / `E-TYP-2635`. Choose a zeroable return type.
- **Non-`public` export → `E-SYS-3353`.** Both `#export` and `#host_export` require `public`.
- **Mixing `#export` and `#host_export` in one assembly → `E-SYS-3358`; `#host_export` outside a library → `E-SYS-3357`.** Pick one foreign-export mode per assembly, and host-export only from a library assembly (`assembly.kind = "library"`).
- **Hosted-export context mistakes.** A missing or non-bundle leading parameter is `E-TYP-2632`; using raw `Context` instead of a projected bundle is `E-TYP-2636`; a `move`-mode leading bundle is `E-TYP-2633`; a generic hosted export is `E-TYP-2634`. Bundle fields must use the reserved names (`io`, `net`, `heap`, `sys`, `reactor`, `time`, `cpu`, `gpu`, `inline`) with the matching `$Class` types.
- **`@null_result` on a non-nullable return → `E-SEM-2856`; `@error` on a `()` return → `E-SEM-2855`.** `@null_result` is valid only for `Ptr<T>@Null`, `*imm T`, or `*mut T`.
- **Unprovable `@foreign_assumes` → `E-SEM-2850`.** Under `#static`, the caller must prove the precondition; switch to `#dynamic` only when a runtime check is the intended semantics (a failure then panics, `P-SEM-2860`).
- **Region-local raw pointer escaping the boundary → `E-SYS-3360`.** Never hand foreign code (or return from an export) a `*imm`/`*mut` pointer into region-local storage. Use storage that outlives the call.
- **Braceless `if` bodies are invalid.** The grammar is `if_tail ::= block_expr (...)`; an `if` body always requires `{ … }`.
- **Redundant attributes warn, not error.** `#unwind("abort")` is `W-SYS-3355`; `#mangle(none)` with `#export("C")` is `W-SYS-3350`. Drop the redundant attribute.
- **`#library` / `#mangle` / `#unwind` misplacement.** `#library` outside an `extern` block is `E-SYS-3345`; an unsupported library kind is `E-SYS-3346`; `#mangle` or `#unwind` on a non-FFI procedure is `E-SYS-3340` / `E-SYS-3356`; two `#unwind` attributes is `E-FFI-0350`; a duplicate boundary symbol name is `E-SYS-3342`; an unknown extern ABI string is `E-SYS-3352`.
