## 11. Pointers, Function Types & Closures

Ultraviolet distinguishes sharply between three kinds of indirection. *Safe pointers* (`Ptr<T>`) carry a statically tracked validity state and panic deterministically on misuse; they are the default tool whenever you need addressable indirection. *Raw pointers* (`*imm T`, `*mut T`) are an `unsafe`-only escape hatch for FFI and low-level work. *Function types* and *closure types* make procedures and closures first-class values you can pass, store, and invoke — the foundation for callbacks, sinks, and higher-order APIs.

This chapter specifies the surface syntax, parsing, static and dynamic semantics, and lowering consequences of each, exactly as defined in §13.8–§13.11 (the type forms) and the expression rules in §16.1, §16.8, and §16.9 that create and consume them. Cross-references to *Arena Allocation & Regions* (`new`, `Region@Active~>alloc`, and the `region` statement), *Permissions* (`const`, `shared`, `unique`), and *Effectful Expressions* are noted where they bear on a rule.

> Terminology note. Throughout this chapter a *place* is an addressable storage location (a binding, field, tuple element, index, or dereference of a place); a *value* is a computed result. Only places have addresses, which is why `&` is restricted to place expressions (§16.8).

> Spelling note. The unit type and the unit value are both written `()` — the empty-tuple type `TypePrim("()")` (rule **Parse-Unit-Type**) and the empty-tuple literal `unit_literal ::= "(" ")"` (§B.1, §B.3). There is no `unit` keyword; a procedure or closure that yields nothing has return type `()` and returns `()`.

---

### 11.1 Safe Pointer Types (`Ptr<T>`)

A safe pointer is a single-machine-word address to a `T`, tagged at the type level with an optional *pointer state*. The state records what the compiler statically knows about the address's validity, and the runtime enforces it: reading or writing through a non-`Valid` pointer is a deterministic panic, never undefined behavior.

#### 11.1.1 Exact syntax

The §13.8.1 production:

```ebnf
safe_ptr_type ::= "Ptr" "<" type ">" ptr_state_opt
ptr_state_opt ::= ε | "@" "Valid" | "@" "Null" | "@" "Expired"
```

The Appendix B form (§B.1) is identical in meaning, written with a folded optional:

```ebnf
safe_pointer_type ::= "Ptr" "<" type ">" ("@" pointer_state)?
pointer_state     ::= "Valid" | "Null" | "Expired"
```

So a safe pointer type is the identifier `Ptr`, an angle-bracketed element type, and an optional `@`-prefixed state name. The three state names are exactly `Valid`, `Null`, and `Expired`. Writing no state (`Ptr<T>`) is *not* a fourth state — it is the unrefined pointer type, written `TypePtr(T, ⊥)` in the AST (§13.8.3).

```text
Ptr<i32>            // unrefined: state unknown (⊥)
Ptr<i32>@Valid      // statically known live
Ptr<i32>@Null       // statically known null
Ptr<i32>@Expired    // statically known dangling (region/scope ended)
Ptr<Ptr<u8>@Valid>  // pointer to a valid pointer-to-u8
```

The parser handles the `Ptr<Ptr<...>>` case by splitting a `>>` token when the inner type closes at the same point as the outer one (rule **Parse-Safe-Pointer-Type-ShiftSplit**, §13.8.2); you write nested pointers with no special spacing.

#### 11.1.2 The pointer-state lattice and its semantics

The AST form (§13.8.3) is `TypePtr(T, state_opt)` with `PtrState = {Valid, Null, Expired}` and the optional `⊥` for the unrefined type. The dynamic meaning of each state (§13.8.5):

| Type | Runtime value | Read / write |
| --- | --- | --- |
| `Ptr<T>@Valid` | `PtrVal(Valid, addr)`, `addr ≠ 0x0` | succeeds (reads/writes the pointee) |
| `Ptr<T>@Null` | `PtrVal(Null, 0x0)` | **panics** |
| `Ptr<T>@Expired` | `PtrVal(Expired, addr)` | **panics** |
| `Ptr<T>` (⊥) | any of the above | dispatches on the runtime state |

Crucially, `@Valid` is not merely a static promise — the runtime *recomputes* the effective state from address tags (§13.8.5):

```text
PtrState(σ, Ptr@Valid(addr)) =
 Valid    if AddrTag(σ, addr) = ⊥
 Valid    if AddrTag(σ, addr) = tag ≠ ⊥ ∧ TagActive(σ, tag)
 Expired  if AddrTag(σ, addr) = tag ≠ ⊥ ∧ ¬ TagActive(σ, tag)
```

That is: a pointer that was `@Valid` into an arena automatically *observes* as `Expired` once that arena's region ends and its tag goes inactive. Reads and writes then panic rather than touching freed storage. The governing rules (§13.8.5):

```text
(ReadPtr-Safe)   PtrState(σ, v_ptr) = Valid  ⇒  read succeeds
(WritePtr-Safe)  PtrState(σ, v_ptr) = Valid  ⇒  write succeeds
(ReadPtr-Null)/(ReadPtr-Expired)   ⇒  Ctrl(Panic)
(WritePtr-Null)/(WritePtr-Expired) ⇒  Ctrl(Panic)
```

#### 11.1.3 Creation: address-of (`&`) and region allocation

There is no `Ptr::new`-style constructor in the core language. A `Ptr<T>@Valid` is produced by taking the address of an addressable place. Region allocation is related, but it creates a value with region provenance; use `&` on the resulting place when pointer indirection is needed.

**Address-of a place (`&`).** The `address_of_expr ::= "&" place_expr` form (§16.8.1) takes the address of an existing place. Its typing rule (**T-AddrOf**, §16.8.4):

```text
Γ; R; L ⊢ p :place T    AddrOfOk(p)
────────────────────────────────────────
Γ; R; L ⊢ AddressOf(p) : TypePtr(T, `Valid`)
```

The operand must be a place. `AddrOfOk(p)` requires `IsPlace(p)`, and additionally:

- if `p` is an index access `a[i]`, the index `i` must have type `usize` (after stripping permission);
- if `p` is a field of a `#layout(packed)` record, the address-of must lie inside an `unsafe` span.

Applying `&` to a non-place is the error `E-TYP-2104`. At runtime (**EvalSigma-AddressOf**, §16.8.5) the result is `Ptr@Valid(addr)` for the place's address.

**Region allocation.** `new value` allocates into the current scoped region.
`region_handle~>alloc(value)` allocates into the region named by the handle.
Both forms store `value` in a region and yield a value of the same type `T`,
carrying that region's provenance. They do not yield `Ptr<T>@Valid`; bind the
allocated value, then take `&binding` to get a pointer to that place. Explicit
allocation through a receiver that is not `unique Region@Active` is rejected by
ordinary method-call typing. See *Arena Allocation & Regions* for the full
lifetime model.

```ultraviolet
/// Sum a value built inside an arena. The arena pointer is created, used, and
/// dropped entirely within the region, so nothing escapes (no E-MEM-3020).
procedure scaledSum(value: i32, factor: i32) -> i32 {
    region as scratch {
        let stored: i32 = new value
        let node: Ptr<i32>@Valid = &stored
        let scaled: i32 = *node * factor
        return scaled
    }
}
```

> Allocate, use, and *consume* arena pointers inside the same region. A `Ptr<T>@Valid` is `@Valid` only while its region is active; returning or storing one past the region's end is exactly the situation the `@Expired` state and the escape diagnostic `E-MEM-3020` exist to catch. Return *values* out of a region, not arena pointers.

#### 11.1.4 Dereference (`*`)

The `deref_expr ::= "*" unary_expr` form (§16.8.1) reads through a pointer. Its value-typing rule for safe pointers (**T-Deref-Ptr**, §16.8.4):

```text
Γ; R; L ⊢ e : TypePtr(T, `Valid`)    BitcopyType(T)
────────────────────────────────────────────────────
Γ; R; L ⊢ Deref(e) : T
```

Two constraints follow directly from the rule:

1. **The pointer must be statically `@Valid`.** Dereferencing a value of type `Ptr<T>@Null` is `E-TYP-2101`; dereferencing `Ptr<T>@Expired` is `E-TYP-2102`. To dereference an unrefined `Ptr<T>` you must first narrow it to `@Valid` (see §11.1.6).
2. **The pointee must be `Bitcopy`.** `*p` as a value expression copies the pointee out, so `T` must be a bitwise-copyable type.

`*p` is also a place: `IsPlace(Deref(p)) ⇔ IsPlace(p)` (§16.8.4), and the §16 place grammar is `place_expr ::= "*" place_expr | postfix_expr`. So `*p = v` writes through the pointer, lowering to `WritePtrSigma`, which panics on a non-`Valid` runtime state. As with every assignment, the *root* binding of the place must be mutable: `*p = v` requires `p` to be a `var` binding (the assignment rule keys mutability on `PlaceRoot`, §19.x). Dynamically, **EvalSigma-Deref** evaluates `e` to a pointer value and then `ReadPtrSigma`, so a `@Valid`-typed pointer that observes as `Expired` at runtime (its arena ended) panics rather than reading freed memory.

```ultraviolet
/// Read through a valid pointer (value form: i32 is Bitcopy).
procedure readThrough(slot: Ptr<i32>@Valid) -> i32 {
    return *slot
}

/// Write through a valid pointer. The root of the place `*p` is the local `p`,
/// which is declared `var` so the assignment typechecks; the write targets the
/// pointee, not the binding.
procedure bumpLocal(start: i32) -> i32 {
    var cell: i32 = start
    var p: Ptr<i32>@Valid = &cell
    *p = *p + 1
    return cell
}
```

#### 11.1.5 Null pointers: `Ptr::null()`

The only expression that produces a safe null pointer is the dedicated form `Ptr::null()` (§16.1.1):

```ebnf
null_ptr_expr ::= "Ptr" "::" "null" "(" ")"
```

It evaluates (**EvalSigma-PtrNull**, §16.1.5) to `Ptr@Null(0x0)`. It has **no synthesizable type of its own** — it must be *checked against* an expected pointer type whose state admits null (**Chk-Null-Ptr**, §8.3, §16.1.4):

```text
PtrNullExpected(T) ⇔ T = TypePtr(U, s) ∧ s ∈ {`Null`, ⊥}
```

So `Ptr::null()` is well-typed only where the expected type is `Ptr<U>@Null` or an unrefined `Ptr<U>`. Used with no expected pointer type — for example as a bare `let x := Ptr::null()` with no annotation — it is the inference failure `E-TYP-1530` (`PtrNull-Infer-Err`). Note also that the *general* null literal `null` checks only against raw-pointer types (`NullLiteralExpected(T) ⇔ T = TypeRawPtr(q, U)`, §16.1.4); for a safe null you must write `Ptr::null()`.

```ultraviolet
/// A nullable handle: the unrefined Ptr<u8> can hold either a valid or a null
/// pointer, so Ptr::null() checks against the declared return type.
procedure emptyHandle() -> Ptr<u8> {
    return Ptr::null()
}
```

#### 11.1.6 Layout, copying, and the `@Valid` niche

A safe pointer is one machine word: `sizeof(Ptr<T>) = sizeof(usize)`, `alignof(Ptr<T>) = alignof(usize)` (§13.8.6). Safe pointers are `Bitcopy` and `Clone` and are **not** `Drop` (§13.8.4): copying a `Ptr<T>` duplicates the address, and dropping one frees nothing — the pointee's lifetime is owned by its arena or place, not the pointer.

The `@Valid` state carries a *niche*: because a valid pointer can never be the all-zero bit pattern (`NicheSet(Ptr<T>@Valid) = {LEBytes(0, PtrSize)}`, §13.8.6), the zero word is available as a discriminant. This is what lets, for example, an optional `Ptr<T>@Valid` (a union with a unit member) occupy a single word with no separate tag. The unrefined `Ptr<T>` and the `@Null`/`@Expired` forms carry no niche.

To go from an unrefined `Ptr<T>` to a usable `@Valid` you narrow on the runtime state (pointer states are modal states; narrow with the modal `if ... is` form described in *Modal Types*), or you obtain a `@Valid` directly from `&` and keep it `@Valid` end-to-end.

---

### 11.2 Raw Pointer Types (`*imm T`, `*mut T`)

Raw pointers are an explicit, `unsafe`-gated escape hatch. They carry **no** state metadata, perform **no** validity tracking, and exist primarily for FFI and the implementation of safe abstractions over foreign memory. The style guide is emphatic (AGENTS.md §`unsafe`, §FFI): prefer safe pointers and safe wrappers; reach for raw pointers only when a safe pattern genuinely cannot express the required behavior, keep the `unsafe` surface minimal, and document every boundary's ownership, lifetime, thread affinity, and caller obligations.

#### 11.2.1 Exact syntax

The §13.9.1 production and the equivalent Appendix B form (§B.1):

```ebnf
raw_ptr_type ::= "*" ("imm" | "mut") type

raw_pointer_type ::= "*" raw_pointer_qual type
raw_pointer_qual ::= "imm" | "mut"
```

A raw pointer is `*`, a mutability qualifier (`imm` for read-only, `mut` for read-write), and an element type. The AST form is `TypeRawPtr(qual, elem)` with `qual ∈ {imm, mut}` (§13.9.3).

```text
*imm u8       // read-only raw pointer to bytes
*mut i32      // read-write raw pointer to i32
*imm *mut u8  // raw pointer to a (mutable raw pointer to u8)
```

#### 11.2.2 Where raw pointers may be used

Raw pointers are well-formed types anywhere (**WF-RawPtr**, §13.9.4), so you may name them in signatures and fields freely. The restriction is on **dereference**, not on the type: every raw `*` dereference must lie within an `unsafe` span. The two place-typing rules (§13.9.4):

```text
(P-Deref-Raw-Imm)  UnsafeSpan(span(Deref(e)))    Γ;R;L ⊢ e : TypeRawPtr(`imm`, T)
                   ⇒ Deref(e) :place TypePerm(`const`, T)
(P-Deref-Raw-Mut)  UnsafeSpan(span(Deref(e)))    Γ;R;L ⊢ e : TypeRawPtr(`mut`, T)
                   ⇒ Deref(e) :place TypePerm(`unique`, T)
```

So dereferencing a `*imm T` yields a `const` place (read-only) and a `*mut T` yields a `unique` place (writable). Dereferencing **either** outside `unsafe` is the error **Deref-Raw-Unsafe** = `E-TYP-2103`:

```text
(Deref-Raw-Unsafe)
Γ; R; L ⊢ e : TypeRawPtr(q, T)    ¬ UnsafeSpan(span(Deref(e)))    c = Code(Deref-Raw-Unsafe)
──────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Deref(e) ⇑ c
```

The value form **T-Deref-Raw** (§16.8.4) additionally requires `UnsafeSpan` *and* the pointee to be `Bitcopy`, exactly as for safe pointers:

```text
(T-Deref-Raw)
UnsafeSpan(span(Deref(e)))    Γ; R; L ⊢ e : TypeRawPtr(q, T)    BitcopyType(T)
──────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Deref(e) : T
```

#### 11.2.3 Dynamic semantics: panics, not UB

Unlike many low-level languages, an invalid raw access in Ultraviolet is a **runtime panic**, not undefined behavior. There are no compile-time diagnostics for bad raw addresses (§13.9.7); the safety net is dynamic (§13.9.5):

```text
(ReadPtr-Raw)          RawPtr(q, addr), ReadAddr defined   ⇒ reads the value
(WritePtr-Raw)         RawPtr(`mut`, addr), WriteAddr ok    ⇒ writes the value
(ReadPtr-Raw-Invalid)  ReadAddr(σ, addr) undefined          ⇒ Ctrl(Panic)
(WritePtr-Raw-Imm)     writing through a `*imm` pointer      ⇒ Ctrl(Panic)
(WritePtr-Raw-Invalid) `*mut` write to a bad address         ⇒ Ctrl(Panic)
```

Note that even a `*imm` raw pointer's *write* path is defined to panic — the `imm`/`mut` distinction is enforced statically by the place permission (`const` vs `unique`) and dynamically by `WritePtr-Raw-Imm`. Raw pointers are one word, with `ValidValue(TypeRawPtr(q, T), bits) ⇔ |bits| = PtrSize` (§13.9.6).

```ultraviolet
/// Read a byte through a foreign pointer obtained at an FFI boundary.
/// The unsafe block wraps exactly the dereference, and the boundary
/// obligation (validity, lifetime, thread affinity) is documented by the caller.
procedure readForeignByte(handle: *imm u8) -> u8 {
    return unsafe { *handle }
}
```

> `unsafe { ... }` is the `unsafe_expr ::= "unsafe" block_expr` form (§16.8.1). It does not change the typing of its body other than discharging the `UnsafeSpan` obligation for the operations within it (**T-Unsafe-Expr**, §16.8.4). Keep it to the single operation that needs it.

---

### 11.3 Function Types

A function type is the type of a named procedure used as a first-class value. It describes a callable with a parameter list (each parameter optionally consuming, via `move`) and a return type.

#### 11.3.1 Exact syntax

The §13.10.1 production:

```ebnf
func_type       ::= "(" param_type_list? ")" "->" type
param_type_list ::= param_type ("," param_type)* ","?
param_type      ::= "move" type | type
```

Appendix B (§B.1) names this the *sparse* function type and groups it with closures under `function_type`:

```ebnf
function_type        ::= sparse_function_type | closure_type
sparse_function_type ::= "(" param_type_list? ")" "->" type
param_type           ::= "move"? type
```

A function type is a parenthesized list of parameter types, then `->`, then the return type. A `move` prefix on a parameter type marks that parameter as *consuming* (ownership-transferring). The return type is **mandatory** — there is no abbreviated form without `-> type`. The AST form is `TypeFunc([⟨mode_1, T_1⟩, …, ⟨mode_n, T_n⟩], R)` where each `mode_i ∈ {move, ⊥}` (§13.10.3).

A trailing comma in `param_type_list` is permitted only where `TrailingCommaAllowed` holds (§5.5) and never denotes an extra parameter (§13.10.1).

```text
(i32, i32) -> i32              // two i32 params, returns i32
() -> ()                       // no params, returns the unit type ()
(move Buffer) -> Digest        // consumes its Buffer argument
(i32, move String,) -> bool    // trailing comma allowed; still two params
```

#### 11.3.2 Procedures as first-class values

Any declared procedure denotes a value of the matching function type (**T-Proc-As-Value**, §13.10.4):

```text
procedure f(m_1 x_1 : T_1, …, m_n x_n : T_n) -> R declared
──────────────────────────────────────────────────────────
Γ ⊢ f : TypeFunc([⟨m_1, T_1⟩, …, ⟨m_n, T_n⟩], R)
```

So referencing a procedure by name produces a `FuncVal` (§13.10.5) you can bind, pass, store in a field, or call. The parameter *mode* of the function type tracks the procedure's parameter modes: a `move`-consuming parameter in the declaration becomes a `move` parameter type. Function types are well-formed when their return and all parameter types are (**WF-Func**, §13.10.4); equivalence and subtyping are the standard structural rules (§8.1, §8.2). Call arity, argument typing, and the callee-kind check are owned by Chapter 16; a call whose callee is not of function (or closure) type is `E-SEM-2531` (`Call-Callee-NotFunc`).

```ultraviolet
procedure addOne(value: i32) -> i32 {
    return value + 1
}

/// A unary numeric operation passed as a first-class value.
procedure applyTwice(op: (i32) -> i32, start: i32) -> i32 {
    return op(op(start))
}

procedure demo() -> i32 {
    return applyTwice(addOne, 0)   // addOne : (i32) -> i32
}
```

Function-type values are lowered through the ordinary call and ABI machinery (§13.10.6); they introduce no diagnostics of their own beyond well-formedness and the shared call diagnostics (§13.10.7).

---

### 11.4 Closure Types and Closure Expressions

A closure is an anonymous callable that may *capture* bindings from its defining scope. Ultraviolet separates two cases at the type level:

- A **non-capturing** closure has the same shape as a procedure and is given a **function type** (`TypeFunc`). It is a bare code pointer with no environment.
- A **capturing** closure is given a **closure type** (`TypeClosure`), a two-word value pairing an environment pointer with a code pointer.

This split (rules **T-Closure-NonCapturing**, **T-Closure-Capturing**, **T-Closure-Escaping**, §16.9.4) is automatic. Because a non-capturing closure may also be checked against an expected closure type (**T-Closure-NonCapturing-Expected**), a closure literal can be passed where either a function type or a closure type is expected.

#### 11.4.1 Closure type syntax

The §13.11.1 production for the *type*:

```ebnf
closure_type      ::= "|" param_type_list? "|" "->" type closure_deps_opt
closure_deps_opt  ::= ε | "[" "shared" ":" "{" shared_dep_list? "}" "]"
shared_dep_list   ::= shared_dep ("," shared_dep)*
shared_dep        ::= identifier ":" type
```

Appendix B (§B.1, identical meaning):

```ebnf
closure_type ::= "|" param_type_list? "|" "->" type closure_deps?
closure_deps ::= "[" "shared" ":" "{" shared_dep_list? "}" "]"
shared_dep   ::= identifier ":" type
```

A closure type uses pipe delimiters `|...|` around the parameter types (reusing the same `param_type_list` as function types, so `move` is allowed), then `->`, then the return type, then an optional **dependency clause** listing the closure's `shared` captures as `[shared: { name: type, ... }]`. The return type is mandatory. The AST form is `TypeClosure(params, ret, deps_opt)` where `deps_opt` is `⊥` or `⟨[⟨name_1, T_1⟩, …]⟩` (§13.11.3).

One disambiguation rule (§13.11.1): inside a closure *type*, a parameter type whose outermost constructor is a `union_type` **must** be parenthesized as `("(" type ")")`. This grouping only disambiguates; it introduces no new type constructor.

```text
|i32| -> i32                                  // one i32 param, returns i32
|| -> ()                                      // no params, returns the unit type ()
|i32| -> i32 [shared: { total: shared i64 }]  // captures `total` by shared ref
|(A | B)| -> bool                             // union param MUST be parenthesized
```

The dependency clause is ordinary type syntax — it may appear anywhere a type is written. For example, as a type alias for a callback that captures a shared counter:

```ultraviolet
/// A sink that consumes an i64 and updates a shared running total.
type DeltaSink = |i64| -> () [shared: { total: shared i64 }]
```

#### 11.4.2 Closure expression syntax

The §16.9.1 / Appendix B (§B.3) production for the *expression* that creates a closure:

```ebnf
closure_expr       ::= "|" closure_param_list? "|" ("->" type)? closure_body
closure_param_list ::= closure_param ("," closure_param)* ","?
closure_param      ::= "move"? identifier (":" type)?
closure_body       ::= expression | block_expr
```

Differences from the closure *type*: each parameter is a name (with optional `move` prefix and optional `: type` annotation), the return type is **optional** (inferred from the body when omitted — **Infer-Closure-Return**, §16.9.4), and the body is either a single expression or a block. The same union-parenthesization rule applies to typed parameter annotations (§16.9.1).

```ultraviolet
let inc      = |x: i32| -> i32 { return x + 1 }   // explicit param type and return
let inc2     = |x: i32| x + 1                      // expression body, return inferred
let constant = || -> i32 42                        // no params, expression body
```

Parameter types may be omitted only when an expected closure/function type makes them inferable (**Infer-Closure-Params**, §16.9.4). A closure parameter with no annotation and no expected type is the error **Infer-Closure-Params-Err** = `E-SEM-2591`.

#### 11.4.3 Capture classification

The compiler computes the captured set from the body (§16.9.4): `CaptureSet(C)` is the free variables of the body minus the parameters. Each capture is then classified by the *permission* of the captured binding:

```text
ConstCaptures(C)  = { x ∈ CaptureSet(C) | Γ(x) = TypePerm(`const`, _) }
SharedCaptures(C) = { x ∈ CaptureSet(C) | Γ(x) = TypePerm(`shared`, _) }
UniqueCaptures(C) = { x ∈ CaptureSet(C) | Γ(x) = TypePerm(`unique`, _) }
```

The rules that govern what may be captured and how:

- **`const` captures** are captured **by reference** (`CaptureMode = ByRef`, **Capture-Const**, §16.9.4). The closure environment stores a `Ptr<T>@Valid` to the binding.
- **`shared` captures** are also captured **by reference** (**Capture-Shared**), and additionally appear in the closure type's `[shared: {...}]` dependency clause when the closure escapes. Both `const` and `shared` captures use the reference-capture environment representation (§16.9.6). Key acquisition for accessing `shared` captures is owned by *Concurrency / Keys* (Chapter 19).
- **`unique` captures are forbidden** for implicit capture. A closure that captures a `unique` binding is **Capture-Unique-Err** = `E-CON-0120`:

  ```text
  (Capture-Unique-Err)
  x ∈ UniqueCaptures(C)    c = Code(Capture-Unique-Err)
  ──────────────────────────────────────────────────────
  Γ; R; L ⊢ C ⇑ c
  ```

  To put owned data into a closure, **move** it in instead (below).

- **`move` captures.** A binding is move-captured if it is named with `move` in the closure's parameter list, or if the body contains `move x` for a captured `x` (`MoveCaptureSet`, §16.9.4). Move captures are stored **by value** in the environment; reference captures are stored as a `Ptr<T>@Valid` (`CaptureType`, §16.9.6). After a closure move-captures `x`, `x` is marked moved in the defining scope (**EvalSigma-Closure-Capturing** calls `MarkMoved`).

Move/ref capture validity is checked against the binding state (§16.9.4):

- move-capturing an already-moved binding is **B-Closure-MoveCapture-Moved-Err** = `E-CON-0121`;
- move-capturing an immovable binding is **B-Closure-MoveCapture-Immovable-Err** = `E-MEM-3006`;
- reference-capturing a moved binding is **B-Closure-RefCapture-Moved-Err** = `E-MEM-3001`.

#### 11.4.4 Local vs escaping closures, and the dependency clause

A closure is **local** if it does not escape its defining scope, and **escaping** if its expected type can outlive that scope. The distinction is `IsEscaping(C) ⇔ ExpectedType(C) ≠ ⊥ ∧ CanEscape(ExpectedType(C))`, where `CanEscape(T)` holds for a closure type or an unbounded generic type (§16.9.4). It controls the type assigned to a *capturing* closure:

- **Local capturing** (**T-Closure-Capturing**) → `TypeClosure(params, R, ⊥)` — no dependency clause.
- **Escaping capturing** (**T-Closure-Escaping**) → `TypeClosure(params, R, ⟨deps⟩)` where `deps` lists the `shared` captures with their types.

Both require `UniqueCaptures(C) = ∅` (consistent with `E-CON-0120`). The dependency set **may be inferred** when the closure is checked against an expected closure type; otherwise it **must be written explicitly** (§16.9.4). This is why a stored or returned escaping closure whose type is written out carries the `[shared: {...}]` clause.

The following is a *local* capturing closure: it captures a `const` binding by reference (no key required) and is used immediately as a callback, so it is non-escaping and carries no dependency clause.

```ultraviolet
/// Apply a const-capturing callback to each of two inputs and sum the results.
/// `bias` is captured by reference (const => ByRef); the closure does not escape.
procedure biasedSum(bias: i32, first: i32, second: i32) -> i32 {
    let shift = |n: i32| -> i32 { return n + bias }
    return shift(first) + shift(second)
}
```

#### 11.4.5 Calling closures and the pipeline operator

Closures are invoked with **ordinary call syntax** (§16.9.1); the call resolves to the internal `ClosureCall` form (**T-ClosureCall**, §16.9.4). A closure value is `ClosureVal(env_ptr, code_ptr)` (§13.11.5, §16.9.5); a non-capturing closure has `env_ptr = null`. Calling passes the environment pointer as a hidden first argument (§16.9.6 lowering).

Ultraviolet also provides the **pipeline operator** `=>` for left-to-right application (§16.9.1):

```ebnf
pipeline_expr ::= base_postfix_expr ("=>" base_postfix_expr)*
```

`e_1 => e_2` is a left-first application form: it evaluates `e_1`, then `e_2`, then applies the resulting function or closure to the left value (§16.9.5). The right-hand side must be a single-argument function or closure (**T-Pipeline**): a non-callable RHS is `E-SEM-2538` (`T-Pipeline-NotCallable-Err`); a type mismatch or wrong arity is `E-SEM-2539` (`T-Pipeline-TypeMismatch-Err`, `T-Pipeline-ArgCount-Err`).

```ultraviolet
procedure double(x: i32) -> i32 {
    return x * 2
}

procedure pipelineDemo() -> i32 {
    let square = |n: i32| -> i32 n * n
    return 3 => double => square     // square(double(3)) == 36
}
```

#### 11.4.6 Closure representation and layout

Every closure type is two machine words regardless of capture count (§13.11.6):

```text
ClosureRep = ⟨env_ptr: *imm u8, code_ptr: *imm u8⟩
sizeof(TypeClosure(...)) = 2 × PtrSize    alignof = PtrAlign
```

A non-capturing closure's `env_ptr` is `null`; a capturing closure's environment is allocated and its fields laid out in deterministic lexicographic name order (`CaptureList`, §16.9.6). Reference captures store a `Ptr<T>@Valid`; move captures store the value inline (`CaptureType`, §16.9.6). Inside the body, reading a reference capture loads the stored pointer and then loads through it (**Lower-CapturedIdent-Ref**); reading a move capture loads the value directly (**Lower-CapturedIdent-Move**). The closure code function takes the environment pointer (`*imm u8`) as its first parameter (`ClosureCodeSig`, §16.9.6).

---

### 11.5 Choosing the Right Indirection

| You need… | Use | Why |
| --- | --- | --- |
| To pass/return a small value | the value itself | No indirection; cheapest and safest. |
| Addressable access to a place | `Ptr<T>@Valid` via `&` | State-tracked, panics on misuse, niche-optimizable. |
| A nullable pointer slot | `Ptr<T>` (or `Ptr<T>@Null`) + `Ptr::null()` | Null is a first-class, checkable state. |
| Arena-scoped allocation | `new` or `Region@Active~>alloc` / `region` + `&` when a pointer is needed | Lifetime tied to the region; pointers auto-`Expired` on exit. |
| A first-class named procedure | `(...) -> R` (function type) | Bare code pointer; no environment. |
| A callback/sink that captures state | a closure → `TypeClosure` | Captures `const`/`shared` by ref, owned data by `move`. |
| FFI / foreign memory | `*imm T` / `*mut T` in `unsafe`, behind a safe wrapper | Last resort; document the boundary. |

The default is always the safe pointer or the plain value. Reach for raw pointers only at FFI boundaries, and only behind a safe API that re-establishes invariants (AGENTS.md §FFI).

---

### 11.6 Idioms & Best Practices

- **Keep pointers `@Valid` end-to-end.** Obtain `Ptr<T>@Valid` from `&` and thread it through `@Valid`-typed signatures so dereference is always statically legal. Only widen to the unrefined `Ptr<T>` when you genuinely need to represent "valid or null/expired", and narrow back with a modal `if ... is` check before dereferencing.
- **Never let an arena pointer outlive its arena.** A `Ptr<T>@Valid` into a `region` becomes observably `Expired` once the region ends; returning or storing it past that point is what `E-MEM-3020` (escape) and the `@Expired` state guard against. Allocate, use, and consume arena pointers inside the same region; return *values* out of regions, not arena pointers.
- **Root deref-writes in a `var` binding.** A write `*p = v` is an assignment whose place root is `p`; the root binding must be `var`. Bind the pointer in a `var` local (or take `&` of a `var` place) before writing through it.
- **Use `Ptr::null()`, not `null`, for safe nulls,** and always in a context with an expected `Ptr<U>` type — a bare `Ptr::null()` with no expected type is `E-TYP-1530`. The plain `null` literal is for raw pointers only.
- **Prefer non-capturing closures and function-typed parameters for stateless callbacks.** A closure that captures nothing is given a `TypeFunc` and lowers to a bare code pointer — cheaper than a two-word closure value. Accept `(T) -> R` parameters when the callback needs no state.
- **Move owned data into closures; never try to capture `unique`.** Implicit `unique` capture is rejected (`E-CON-0120`). Use a `move` parameter (`|move buf: Buffer| ...`) or `move x` in the body to transfer ownership into the environment.
- **Write the `[shared: {...}]` dependency clause on stored/returned escaping closures.** It is inferable only when checking against an expected closure type; an escaping closure whose type is written explicitly must spell out its `shared` captures.
- **Confine `unsafe` to the smallest possible span.** Per AGENTS.md, an `unsafe` block should wrap exactly the raw dereference that needs it, inside a safe wrapper procedure that documents ownership, lifetime, thread affinity, and caller obligations. Raw pointers may appear in types freely; only their *dereference* requires `unsafe`.
- **Honor naming conventions.** Named procedures used as values follow `camelCase`; the `snake_case` locals that hold procedure or closure values follow the local-variable convention; pointer element types and closure-type aliases are `PascalCase` (AGENTS.md §Naming). Write explicit `return` in non-`()` procedure and block-bodied closure bodies; expression-body closures may omit it.

---

### 11.7 Pitfalls & Diagnostics

| Symptom | Diagnostic | Cause and fix |
| --- | --- | --- |
| Dereferencing a known-null safe pointer | `E-TYP-2101` | `*p` where `p : Ptr<T>@Null`. Narrow to `@Valid` first, or guard the null case. |
| Dereferencing a known-expired safe pointer | `E-TYP-2102` | The pointer's arena/scope ended. Do not let arena pointers escape their region. |
| Raw dereference outside `unsafe` | `E-TYP-2103` (`Deref-Raw-Unsafe`) | Wrap the `*` of a `*imm`/`*mut` in an `unsafe { ... }` block. |
| `&` on a non-place | `E-TYP-2104` | Address-of requires a place (binding/field/tuple element/index/deref of a place), not a computed value. |
| `&` of a `#layout(packed)` field outside `unsafe` | address-of packed-field diagnostic (§16.8.7) | Packed-field address-of needs `unsafe`; or copy the field out first. |
| Non-`usize` index inside an address-of | address-of non-`usize` index diagnostic (§16.8.7) | The index `i` in `&a[i]` must have type `usize`. |
| `Ptr::null()` with no expected pointer type | `E-TYP-1530` (`PtrNull-Infer-Err`) | Annotate the binding, or use it where a `Ptr<U>` is expected. |
| `new` allocation with no active region | `E-MEM-3021` | Open a `region`/`frame` first, or use an explicit `Region@Active` handle such as `r~>alloc(value)`. |
| Arena pointer escapes its region | `E-MEM-3020` | A shorter-lived provenance reaches a longer-lived location. Return a value, not the pointer. |
| Closure parameter type cannot be inferred | `E-SEM-2591` (`Infer-Closure-Params-Err`) | Annotate the parameter (`|x: i32| ...`) or supply an expected closure/function type. |
| Implicit capture of a `unique` binding | `E-CON-0120` (`Capture-Unique-Err`) | Move the binding into the closure (`move`) instead of capturing it by reference. |
| Move-capturing an already-moved binding | `E-CON-0121` (`B-Closure-MoveCapture-Moved-Err`) | The binding was already consumed; restructure so it is live at capture. |
| Move-capturing an immovable binding | `E-MEM-3006` (`B-Closure-MoveCapture-Immovable-Err`) | A `:=` binding cannot be moved; capture by reference or redesign ownership. |
| Reference-capturing a moved binding | `E-MEM-3001` (`B-Closure-RefCapture-Moved-Err`) | The captured binding was moved away before the closure was formed. |
| Calling a non-function value | `E-SEM-2531` (`Call-Callee-NotFunc`) | The callee is not of function/closure type. |
| Pipeline RHS not callable | `E-SEM-2538` (`T-Pipeline-NotCallable-Err`) | `=>` requires a single-argument function/closure on the right. |
| Pipeline type/arity mismatch | `E-SEM-2539` (`T-Pipeline-TypeMismatch-Err`, `T-Pipeline-ArgCount-Err`) | The piped value's type must subtype the RHS's one parameter type, and the RHS must take exactly one parameter. |

Two non-obvious traps worth restating. First, a `Ptr<T>@Valid` is **not** a static guarantee that survives region teardown: the runtime recomputes state from address tags, so a `@Valid`-typed pointer into a dropped arena panics on use rather than reading freed memory — correct, but a panic nonetheless, so manage lifetimes deliberately. Second, the *unrefined* `Ptr<T>` cannot be dereferenced as-is (**T-Deref-Ptr** demands `@Valid`); it exists to model "maybe null/expired", and you must narrow it before reading through it.

Related chapters: *Arena Allocation & Regions* (`new`, `Region@Active~>alloc`, `region` statements, and the lifetime model behind `@Expired`), *Permissions* (`const`/`shared`/`unique`, which drive closure capture classification), *Modal Types* (narrowing pointer states with `if ... is`), *Effectful Expressions* (`&`, `*`, `move`, `copy`, `unsafe`), and *Concurrency & Keys* (Chapter 19 — key acquisition for accessing the `shared` captures of escaping closures).
