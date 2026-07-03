## 14. Opaque, Refinement & Capability Classes

This chapter covers four closely related parts of Ultraviolet's type system that all govern *what a value lets you do* rather than *what a value is made of*: **opaque types** (§14.7), which hide a concrete representation behind a class interface across a module boundary; **refinement types** (§14.8), which attach a statically-checked predicate to an existing type; **capability classes** (§14.9), the `$`-typed authority handles such as `$IO` that gate every externally-observable effect; and the **foundational classes** (§14.10) — `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, `GpuSafe`, `Eq`, `Hasher`, `Hash`, `Iterator`, and `Discrete` — that the language interprets intrinsically. It closes with the consolidated refinement/polymorphism diagnostics supplement (§14.11).

These features build directly on the class machinery of this chapter's earlier sections: ordinary class declarations (§14.3), the concrete-implementer relation (§14.4), associated types (§14.5), and dynamic class objects (§14.6, the `$Class` form). Read those first; this chapter relies on their grammar and judgments without re-deriving them.

A note on tokens before we start. Every syntactic claim below is reproduced from the specification and Appendix B verbatim. Two pairs of forms are routinely confused and are called out explicitly where they appear: the refinement operator `|:` versus the union bar `|`; and the receiver shorthands `~` (`const`), `~!` (`unique`), and `~%` (`shared`). Ultraviolet has **no** `mut` keyword and **no** `while` keyword — mutable bindings use `var`, and condition loops use `loop <condition> { … }`. The dynamic-verification attribute is spelled `#dynamic` (the specification's canonical attribute token); the style guide refers to the same concept in prose as `[[dynamic]]`, but `#dynamic` is what compiles.

### 14.7 Opaque Types

An opaque type names a class and exposes *only* that class's interface, while the actual returned value keeps a concrete representation the caller cannot observe. This is the principal tool for hiding representation across a module boundary: a procedure can return `opaque Counter` and callers may call `Counter`'s methods but can never see, match, or depend on the concrete type chosen by the implementing body.

#### 14.7.1 Syntax

The opaque type is a single grammar production. Its operand is a `class_path` (a `type_path`).

```ebnf
opaque_type ::= "opaque" class_path
class_path  ::= type_path
```

From the Appendix B type grammar, `opaque_type` is one of the alternatives of `non_union_type`, so it composes wherever a `type` is accepted — return positions, bindings, fields, parameters:

```ebnf
type                ::= permission? non_permission_type refinement_clause?
non_permission_type ::= union_type | non_union_type
non_union_type      ::= primitive_type | tuple_type | array_type | slice_type | function_type
                      | closure_type | safe_pointer_type | raw_pointer_type | string_type
                      | bytes_type | dynamic_type | opaque_type | state_specific_type | nominal_type
```

The specification states this explicitly: "Opaque types are type forms and therefore compose with the ordinary declaration and return-type syntactic positions that accept `type`."

#### 14.7.2 Parsing

Parsing recognizes the `opaque` lexeme followed by a class path, producing `TypeOpaque(path)`:

```text
(Parse-Opaque-Type)
IsIdent(Tok(P))    Lexeme(Tok(P)) = `opaque`    Γ ⊢ ParseTypePath(Advance(P)) ⇓ (P_1, path)
────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseNonPermType(P) ⇓ (P_1, TypeOpaque(path))
```

#### 14.7.3 AST Representation

```text
Type       = TypeOpaque(path) | …
TypeOpaque = ⟨path⟩
```

#### 14.7.4 Static Semantics

**Well-formedness.** An opaque type is well-formed exactly when its class path names a declared class:

```text
(WF-Opaque)
T = TypeOpaque(path)    path ∈ dom(Σ.Classes)
─────────────────────────────────────────────
Γ ⊢ T wf

(WF-Opaque-Err)
T = TypeOpaque(path)    path ∉ dom(Σ.Classes)
─────────────────────────────────────────────
Γ ⊢ T wf ⇑
```

**Equivalence.** Two opaque types are equivalent exactly when they name the same class path (rule **(T-Equiv-Opaque)**, defined once by §8.1). Opaque values expose only the class interface named by that path.

**Returning an opaque value.** A procedure whose return type is `opaque Cl` must produce a body value whose type is a subtype of `Cl`:

```text
(T-Opaque-Return)
Γ ⊢ body : T    Γ ⊢ T <: Cl    return_type(f) = opaque Cl
─────────────────────────────────────────────────────────
Γ ⊢ f : () → opaque Cl
```

If the body's type does not implement the named class, the program is ill-formed with `E-TYP-2511` ("Opaque return type does not implement required class").

**Projecting through the interface.** Callers may invoke only methods in the named class's interface:

```text
(T-Opaque-Project)
Γ ⊢ f() : opaque Cl    m ∈ interface(Cl)
────────────────────────────────────────
Γ ⊢ f()~>m(args) : R_m
```

Accessing any member not in `Cl`'s interface — fields of the hidden concrete type, or methods only that type defines — is `E-TYP-2510` ("Accessing member not defined on opaque type's class"). Assigning or matching between two opaque types whose class paths differ is `E-TYP-2512` ("Attempting to assign incompatible opaque types").

#### 14.7.5 Dynamic Semantics

Opaque types add no runtime wrapper. The callee returns a concrete value implementing the named class, and the caller observes that value only through the statically-restricted opaque interface. There is no boxing, no vtable, and no indirection introduced by opacity itself.

#### 14.7.6 Lowering

Opaque types incur no distinct runtime representation or ABI form. Lowering uses the underlying concrete type chosen by the defining body. An `opaque Cl` value has exactly the layout of the concrete type the body returned.

#### 14.7.7 Worked Example

```ultraviolet
public class Counter {
    public procedure value(~) -> u64
}

public record SaturatingCounter <: Counter {
    public count: u64

    public procedure value(~) -> u64 {
        return self.count
    }
}

/// Returns a fresh counter. Callers see only the `Counter` interface;
/// the `SaturatingCounter` representation is hidden.
public procedure freshCounter() -> opaque Counter {
    return SaturatingCounter { count: 0 }
}

/// Produces a counter one tick higher, again exposing only `Counter`.
/// The concrete reconstruction stays inside this module-private body.
public procedure bumpedCounter(prior: opaque Counter) -> opaque Counter {
    return SaturatingCounter { count: prior~>value() + 1 }
}

public procedure useCounter() -> u64 {
    let c: opaque Counter = freshCounter()
    let next: opaque Counter = bumpedCounter(c)
    return next~>value()
}
```

Here `freshCounter` and `bumpedCounter` satisfy **(T-Opaque-Return)** because `SaturatingCounter <: Counter`. In `useCounter`, `c~>value()` (inside `bumpedCounter`) and `next~>value()` are admitted by **(T-Opaque-Project)** because `value` is in `Counter`'s interface. Writing `c.count` would be rejected by `E-TYP-2510`: `count` is a field of the hidden `SaturatingCounter`, not a member of `Counter`. The reconstruction `SaturatingCounter { count: … }` is legal because it is the *defining* body that owns the concrete representation — callers in another module cannot name `SaturatingCounter` through an `opaque Counter` value at all.

### 14.8 Refinement Types

A refinement type pairs a base type with a pure boolean predicate that constrains its values. It lets you state machine-checkable invariants — "this `u32` is nonzero", "this index is in bounds" — directly in the type, rather than as a runtime guard or a comment. Refinements are statically verified by default; they only emit a runtime check inside an explicit `#dynamic` scope.

#### 14.8.1 Syntax

There are two surface forms, and the distinction is exact.

**1. Standalone refinement type alias.** This binds a name to a refined base type. Inside the predicate, `self` denotes the constrained value. The specification gives this dedicated production in §14.8.1:

```ebnf
type_alias_decl ::= visibility? "type" identifier "=" type "|:" "{" predicate_expr "}"
```

**2. Inline refinement clause.** A `refinement_clause` may be attached to *any* `type` occurrence — a parameter type, a field type, a binding type — as the optional third part of the general `type` production (Appendix B):

```ebnf
type              ::= permission? non_permission_type refinement_clause?
refinement_clause ::= "|:" "{" predicate_expr "}"
predicate_expr    ::= logical_or_expr
```

The general type-alias declaration is the broader form from §12.9.1 (and Appendix B), which carries optional generic parameters:

```ebnf
type_alias_decl ::= attribute_list? visibility? "type" identifier generic_params? "=" type
```

A refinement attached to a *parameter's* type is an **inline parameter constraint**. There is no separate parameter-constraint production. Crucially, the inline form's predicate references the parameter **by name** and **MUST NOT use `self`** — doing so is `E-TYP-1956`. Only the standalone refinement-type-alias form binds `self` to the constrained value.

#### 14.8.2 Parsing

Refinement parsing is an optional suffix. Absent the `|:` operator, no refinement is produced:

```text
(Parse-RefinementOpt-None)
¬ IsOp(Tok(P), "|:")
──────────────────────────────────
Γ ⊢ ParseRefinementOpt(P) ⇓ (P, ⊥)

(Parse-RefinementOpt-Yes)
IsOp(Tok(P), "|:")    IsPunc(Tok(Advance(P)), "{")    Γ ⊢ ParsePredicateExpr(Advance(Advance(P))) ⇓ (P_1, pred)    IsPunc(Tok(P_1), "}")
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseRefinementOpt(P) ⇓ (Advance(P_1), pred)
```

The predicate is parsed as an ordinary expression, and the `predicate_expr` nonterminal expands to `logical_or_expr`:

```text
ParsePredicateExpr(P) ⇓ (P_1, e) ⇔ Γ ⊢ ParseExpr(P) ⇓ (P_1, e)
```

The operator token is `|:` — a single operator in `OperatorSet`, not a union bar `|` followed by a colon. Do not confuse it with the union-type bar `|`.

#### 14.8.3 AST Representation

```text
Type       = TypeRefine(base, pred) | …
TypeRefine = ⟨base, pred⟩

PredicateEquiv(P_1, P_2) ⇔ ∀ σ. (Eval(P_1, σ) = true ⇔ Eval(P_2, σ) = true)
```

Two predicates are equivalent when they agree on every state — refinement equivalence is extensional, not syntactic.

#### 14.8.4 Static Semantics

**Well-formedness.** A refinement is well-formed when its base type is well-formed and the predicate, typed with `self : T` in scope, has type `bool` and is **pure**:

```text
(WF-Refine-Type)
Γ ⊢ T wf    Γ, `self` : T ⊢ P : `bool`    Pure(P)
──────────────────────────────────────────────────
Γ ⊢ (T |: {P}) wf
```

A predicate that is not pure is `E-TYP-1954`; one that does not have type `bool` is `E-TYP-1955`; a refinement whose predicate forms a circular type dependency is `E-TYP-1957`.

**Introduction.** A value of base type `T` acquires the refinement when the predicate, with the value substituted for `self`, is established at a location dominating the current one:

```text
(T-Refine-Intro)
Γ ⊢ e : T    Γ ⊢ F(P[e/`self`], L)    L dominates current location
──────────────────────────────────────────────────────────────────
Γ ⊢ e : T |: {P}
```

**Elimination.** A refined value is always usable as its base type — the refinement only adds information:

```text
(T-Refine-Elim)
Γ ⊢ e : T |: {P}
────────────────
Γ ⊢ e : T
```

**Subtyping.** A refined type is a subtype of its base, and a stronger predicate refines into a weaker one when implication holds:

```text
Γ ⊢ (T |: {P}) <: T

Γ ⊢ P ⇒ Q
───────────────────────────
Γ ⊢ (T |: {P}) <: (T |: {Q})
```

Equivalence and normalization use **(T-Equiv-Refine)** and **(T-Equiv-Refine-Norm)**, defined once by §8.1.

**The decidable fragment.** Implementations MUST support at least: literal comparisons, bound propagation from control flow, syntactic equality up to alpha-renaming, transitive integer inequalities, and boolean combinations thereof. Predicates outside this fragment are not guaranteed provable.

**Verification mode.** Refinement predicates are statically verified by default. If proof fails **outside** a `#dynamic` scope, the program is ill-formed (`E-TYP-1953`). If proof fails **inside** `#dynamic`, lowering inserts a runtime check instead. Note that `#dynamic` applied **directly** to a type-alias declaration is itself ill-formed — `E-CON-0411` — and `#dynamic` applied directly to a field declaration is `E-CON-0412`; so a `#dynamic` scope is established by an enclosing declaration or attributed expression, never by annotating the alias or field.

#### 14.8.5 Dynamic Semantics

Refinement types do not alter the underlying value representation. A `u32 |: {…}` is laid out exactly as a `u32`. The only runtime effect is the optional check inserted under `#dynamic`; a failed dynamically-inserted refinement check **panics** (`P-TYP-1953`).

#### 14.8.6 Lowering

Layout follows the base type via **(LLVMTy-Refine)**, defined once by §24.7.7. Feature-local lowering consists only of optional runtime predicate checks when static verification is not discharged inside `#dynamic` scopes — and nowhere else.

#### 14.8.7 Worked Examples

**Standalone refinement type alias** (uses `self`):

```ultraviolet
/// A `u32` known to be nonzero. The predicate is proven at construction.
public type NonZeroU32 = u32 |: { self != 0 }

public procedure scaleBy(divisor: NonZeroU32, value: u32) -> u32 {
    return value / divisor
}

public procedure run() -> u32 {
    let d: NonZeroU32 = 7
    return scaleBy(d, 84)
}
```

The binding `let d: NonZeroU32 = 7` is admitted by **(T-Refine-Intro)**: the literal `7` makes `7 != 0` provable in the decidable fragment.

**Inline parameter constraint** (references the parameter **by name**, never `self`):

```ultraviolet
/// `count` is constrained inline; the predicate names the parameter, not `self`.
/// Note: `var` declares the mutable bindings, and `loop <cond>` is the condition loop —
/// Ultraviolet has no `mut` keyword and no `while` keyword.
public procedure repeatTick(count: u32 |: { count <= 16 }) -> u32 {
    var total: u32 = 0
    var i: u32 = 0
    loop i < count {
        total = total + 1
        i = i + 1
    }
    return total
}
```

Writing the inline predicate as `{ self <= 16 }` here would be rejected with `E-TYP-1956`, because `self` is meaningful only in a standalone refinement type alias.

### 14.9 Capability Classes

Capability classes are the heart of Ultraviolet's authority model. A *capability* is a first-class, unforgeable value that authorizes an externally-observable effect: reading a file, opening a socket, allocating heap memory, reading a clock, exiting the process. There is **no ambient authority** — a procedure can perform an effect only if it was handed the corresponding capability value. The `$Class` dynamic type (the `dynamic_type` form of §14.6) is how a capability is named in a type: `$IO`, `$Network`, `$HeapAllocator`, and so on.

#### 14.9.1 Syntax

Capability classes introduce **no new surface grammar**. They reuse the ordinary class syntax of §14.3 and the dynamic class type syntax of §14.6:

```ebnf
dynamic_type ::= "$" class_path
class_path   ::= type_path
```

So a capability appears in a type as `$IO`, in a parameter list as `io: $IO`, and is consumed through the ordinary dynamic method-call surface `base~>name(args)`. A *user* capability class is declared with the ordinary `class_decl` and a capability superclass via `<:`:

```ebnf
class_decl        ::= attribute_list? visibility? "modal"? "class" identifier generic_params?
                      ("<:" superclass_bounds)? "{" class_body? "}"
superclass_bounds ::= class_bound ("+" class_bound)*
```

#### 14.9.2 Parsing

Capability classes have no feature-specific parser beyond ordinary class parsing and `$Class` type parsing (**(Parse-Dynamic-Type)**, §14.6.2):

```text
(Parse-Dynamic-Type)
IsOp(Tok(P), "$")    Γ ⊢ ParseTypePath(Advance(P)) ⇓ (P_1, path)
────────────────────────────────────────────────────────────────
Γ ⊢ ParseNonPermType(P) ⇓ (P_1, TypeDynamic(path))
```

#### 14.9.3 The Capability Universe and `CapClass`

The capability-class universe is **open**. A class path `p` is a capability class iff `CapClass(p)` holds (§6.1.1):

```text
BuiltinCapabilityClass = {IO, Network, HeapAllocator, Reactor, ExecutionDomain, System, Time, MonotonicTime, WallTime}

CapClass(p) ⇔ p ∈ BuiltinCapabilityClass ∨ (ClassDecl(p) = C ∧ ∃ B ∈ SuperclassPaths(C). CapClass(B))
```

That is: the nine **built-in root capability classes** are `IO`, `Network`, `HeapAllocator`, `ExecutionDomain`, `System`, `Reactor`, `Time`, `MonotonicTime`, and `WallTime`; and any user class that declares a capability superclass via `<:` is itself a capability class. Capability classhood is determined by the superclass relation **alone** — it MUST NOT depend on attributes or naming. The type-system spelling of a capability class is fixed by `CapType(Cl) = TypeDynamic(Cl)` — i.e. `$Cl`. The nine built-in names are **reserved**.

**Built-in interfaces.** Each built-in capability has a fixed method interface. The `IO` interface (`IOInterface`) is the largest; its methods (all on a `const` receiver) are:

| Method | Parameters | Return |
| --- | --- | --- |
| `open_read` | `path: string@View` | `File@Read \| IoError` |
| `open_write` | `path: string@View` | `File@Write \| IoError` |
| `open_append` | `path: string@View` | `File@Append \| IoError` |
| `create_write` | `path: string@View` | `File@Write \| IoError` |
| `read_file` | `path: string@View` | `Outcome<unique string@Managed, IoError>` |
| `read_bytes` | `path: string@View` | `Outcome<unique bytes@Managed, IoError>` |
| `write_file` | `path: string@View, data: bytes@View` | `Outcome<(), IoError>` |
| `write_stdout` | `data: string@View` | `Outcome<(), IoError>` |
| `write_stderr` | `data: string@View` | `Outcome<(), IoError>` |
| `exists` | `path: string@View` | `bool` |
| `remove` | `path: string@View` | `Outcome<(), IoError>` |
| `open_dir` | `path: string@View` | `DirIter@Open \| IoError` |
| `create_dir` | `path: string@View` | `Outcome<(), IoError>` |
| `ensure_dir` | `path: string@View` | `Outcome<(), IoError>` |
| `kind` | `path: string@View` | `FileKind \| IoError` |
| `restrict` | `path: string@View` | `$IO` |

The other built-in interfaces are smaller (all methods on a `const` receiver):

- `NetworkInterface`: `restrict_to_host(host: string@View) -> $Network`.
- `HeapAllocatorInterface`: `with_quota(size: usize) -> $HeapAllocator`, `alloc_raw(count: usize) -> *mut u8`, and `dealloc_raw(ptr: *mut u8, count: usize) -> ()`.
- `TimeInterface` mints sub-capabilities: `monotonic() -> $MonotonicTime` and `wall() -> $WallTime`.
- `MonotonicTimeInterface`: `now() -> MonotonicInstant`, `resolution() -> Duration`, `elapsed(start: MonotonicInstant, end: MonotonicInstant) -> Outcome<Duration, TimeError>`, and `coarsen(resolution: Duration) -> Outcome<$MonotonicTime, TimeError>`.
- `WallTimeInterface`: `now_utc() -> Outcome<UtcInstant, TimeError>`, `resolution() -> Outcome<Duration, TimeError>`, and `coarsen(resolution: Duration) -> Outcome<$WallTime, TimeError>`.
- `SystemInterface`: `exit(code: i32) -> !`, `get_env(key: string@View) -> string@View`, `executable_path() -> string@View`, `argument_count() -> usize`, `argument(index: usize) -> string@View`, `current_directory() -> string@View`, and `run(command: string@View) -> i32`.
- `Reactor` exposes generic `run` and `register` methods over `Future<T, E>` (these are built-ins lowered to primitives — see §14.9.6 — not a template for user `$`-dispatched methods).

The associated built-in types are: `File`, `DirIter`, `DirEntry`, `FileKind`, and `IoError` (for `IO`); and `Duration`, `MonotonicInstant`, `UtcInstant`, and `TimeError` (for `Time`). For example, `IoError` is the enum with variants `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidPath`, `Busy`, `IoFailure`, and `DirectoryNotEmpty`; `FileKind` is the enum `File | Dir | Other`.

#### 14.9.4 Static Semantics

Capability classes are **ordinary classes** in the type system. A parameter of type `$Class` accepts any concrete type implementing `Class`. Capability classes MAY be used as generic bounds exactly like any other class bound.

`alloc_raw` and `dealloc_raw` on `$HeapAllocator` require an `unsafe` context — calling them outside one is ill-formed:

```text
(AllocRaw-Unsafe-Err)
Γ; R; L ⊢ base : TypeDynamic(`HeapAllocator`)    ¬ UnsafeSpan(span(MethodCall(base, "alloc_raw", args)))    c = Code(AllocRaw-Unsafe-Err)
─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ MethodCall(base, "alloc_raw", args) ⇑ c

(DeallocRaw-Unsafe-Err)
Γ; R; L ⊢ base : TypeDynamic(`HeapAllocator`)    ¬ UnsafeSpan(span(MethodCall(base, "dealloc_raw", args)))    c = Code(DeallocRaw-Unsafe-Err)
───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ MethodCall(base, "dealloc_raw", args) ⇑ c
```

**Capability effect tracking.** `CapInType` (§6.1.1) computes which capabilities a type carries. A `$p` carries `{p}` when `p` is a capability class and `∅` otherwise, and `Context` carries its built-in root set:

```text
CapInType(TypePath([`Context`])) = {IO, Network, HeapAllocator, Reactor, ExecutionDomain, System, Time}
CapInType(TypeDynamic([p])) = {p}    if CapClass(p)
CapInType(TypeDynamic([p])) = ∅      if ¬ CapClass(p)
```

Note that `CapInType(Context)` lists exactly those seven roots; `MonotonicTime` and `WallTime` are *derived* from `Time` rather than carried directly. `CapUp` makes a derived capability satisfy requirements stated against its capability ancestors; `CapDerive` grants what a class's own interface can mint (e.g. `DeriveSet(Time) = {MonotonicTime, WallTime}`). `EffectiveCaps(T) = ⋃{ CapUp(c) ∪ CapDerive(c) | c ∈ CapInType(T) }`.

The cross-call discipline that enforces no-ambient-authority is stated in terms of these sets: for every direct call from `d_src` to `d_tgt`, the program is rejected unless `CapReq(d_tgt) ⊆ EffectiveCapReq(d_src)`, where `CapReq` is the union of `CapInType` over a declaration's parameter and receiver types and `EffectiveCapReq` is the same union over `EffectiveCaps`.

#### 14.9.5 The Authority / Context Model

Capabilities tie directly to the `Context` bundle and the **No Ambient Authority** rules (§6.1.2):

- **(NAA-1)** No implicit capability roots. A conforming implementation MUST NOT provide any implicit or global binding whose type is capability-bearing under `CapInType`.
- **(NAA-2)** `Context` is the **sole** explicit root carrier. The only capability roots introduced at runtime are those inside `Context` values produced by `ContextInitSigma` (executable entry, §24.4.5) or `HostSessionInitSigma` (hosted-library session, §24.4.4).
- **(NAA-3)** Effect gating. Any externally-observable effect occurs only as a consequence of calling a runtime host primitive (classified in §6.2) or a built-in procedure/method whose receiver is a capability value.
- **(NAA-4)** User capabilities confer no new root authority. Constructing a value of a user-defined capability class requires no ambient grant; it confers authority only through the built-in capability values it encapsulates, and every observable effect remains gated by NAA-3.

`Context` is the built-in record bundling the roots. Its fields are `io: $IO`, `net: $Network`, `heap: $HeapAllocator`, `sys: $System`, `reactor: $Reactor`, and `time: $Time`; and it has methods `cpu()`, `gpu()`, and `inline()`, each returning `$ExecutionDomain`.

`Context` enters a program at exactly one place: the entry procedure `main`. `MainSigOk` requires `main` to be `public`, take a single parameter (with no mode, or `move`) whose stripped type is a **context bundle type**, and return `i32`:

```text
MainSigOk(d) ⇔ d = ProcedureDecl(_, vis, `main`, _, _, params, ret_opt, _, _, _, _)
             ∧ vis = `public` ∧ params = [⟨mode, name, ty⟩] ∧ mode ∈ {⊥, `move`}
             ∧ ContextBundleType(StripPerm(ty)) ∧ ret_opt = TypePrim("i32")
```

A *context bundle type* (`ContextBundleType`, §14.9.4) is `Context` itself, or any record whose fields are each either a capability field of the expected built-in type (`io: $IO`, `net: $Network`, `heap: $HeapAllocator`, `sys: $System`, `reactor: $Reactor`, `time: $Time`, or an execution-domain field) or another context bundle type. This lets a program declare a **narrow projected context** — a record holding only the capabilities it actually uses — and still receive it at `main`, because the runtime builds it from the full `Context` via `ContextBundleBuild`.

#### 14.9.6 Dynamic Semantics and Lowering

Capability classes introduce no separate dispatch model: a capability value is expressed through the same dynamic-class-object machinery as any dispatchable class (§14.6). Built-in capability operations have primitive implementations. Calls on dynamic receivers of the nine built-in capability classes lower to **builtin method symbols** rather than emitted vtable-call sequences; other (user) capability classes lower through the ordinary dynamic-dispatch path of §14.6.

#### 14.9.7 Worked Examples

**Narrow capability threading.** Pass only the `$IO` capability, not the whole `Context`:

```ultraviolet
/// Writes a greeting using only the IO capability.
public procedure greet(io: $IO) -> Outcome<(), IoError> {
    return io~>write_stdout("hello\n")
}

public procedure main(ctx: Context) -> i32 {
    let _ = greet(ctx.io)
    return 0
}
```

`greet` receives authority to perform output and nothing else; it cannot touch the network or the filesystem because it was never handed `$Network` or a write path. The cross-call rule holds: `CapReq(greet) = {IO}` and `EffectiveCapReq(main) = EffectiveCaps(Context) ⊇ {IO}`, so `CapReq(greet) ⊆ EffectiveCapReq(main)`. This is capability narrowing as API design (style guide: "Keep authority narrow. Pass only the capabilities and data that are actually used").

**Projected context bundle at `main`.** Declare exactly the roots needed:

```ultraviolet
/// A narrow context carrying only the capabilities this program uses.
public record AppContext {
    public io: $IO
    public time: $Time
}

public procedure main(ctx: AppContext) -> i32 {
    let clock: $MonotonicTime = ctx.time~>monotonic()
    let _ = ctx.io~>write_stdout("started\n")
    return 0
}
```

`AppContext` is a valid bundle type because `io` and `time` are capability fields of the expected built-in types, so `ContextBundleType(AppContext)` holds and `MainSigOk` is satisfied. The runtime synthesizes the `AppContext` value from the full `Context` via `ContextBundleBuild`.

**User capability class** via a capability superclass:

```ultraviolet
/// A user capability: authority to append audit lines. It is a capability class
/// because it declares `IO` as a superclass via `<:`.
public class AuditLog <: IO {
    public procedure record(~, line: string@View) -> Outcome<(), IoError>
}
```

Because `AuditLog` has the capability superclass `IO`, `CapClass(AuditLog)` holds, and `$AuditLog` is a capability type that — through `CapUp` — also satisfies requirements stated against `$IO`.

### 14.10 Foundational Classes

The foundational classes are the small set the language treats **intrinsically**: `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, `GpuSafe`, `Eq`, `Hasher`, `Hash`, `Iterator`, and `Discrete`. Their bounds are not always discharged by ordinary class-implementation lookup; several are satisfied by built-in judgments over the type's structure.

#### 14.10.1 Syntax

Foundational classes use ordinary class syntax (§14.3). The ten names above are **reserved**. `Bitcopy`, `Clone`, `Drop`, `FfiSafe`, and `GpuSafe` are demanded as ordinary generic class bounds on the constrained parameter:

```ebnf
generic_param ::= identifier ("<:" class_bound ("," class_bound)*)? ("=" type)?
class_bound   ::= type_path generic_args?
```

The `|:` token is reserved for contract clauses, invariants, and refinement clauses such as `|: { … }` (§14.8). It does not introduce foundational generic constraints.

#### 14.10.2 AST Representation and Structural Relations

```text
FoundationalClassName = {`Bitcopy`, `Clone`, `Drop`, `FfiSafe`, `GpuSafe`, `Eq`, `Hasher`, `Hash`, `Iterator`, `Discrete`}
```

A type has a user `clone`/`drop` method when its signature matches exactly:

```text
HasCloneMethod(T) ⇔ ∃ p, R, m. T = TypePath(p) ∧ RecordDecl(p) = R ∧ m ∈ Methods(R)
                    ∧ MethodName(m) = `clone` ∧ Sig_T(T, m) = ⟨TypePerm(`const`, T), [], T⟩
HasDropMethod(T)  ⇔ ∃ p, R, m. T = TypePath(p) ∧ RecordDecl(p) = R ∧ m ∈ Methods(R)
                    ∧ MethodName(m) = `drop` ∧ Sig_T(T, m) = ⟨TypePerm(`unique`, T), [], TypePrim("()")⟩
```

The derived structural relations:

```text
CloneType(T) ⇔ BuiltinCloneType(T) ∨ HasCloneMethod(StripPerm(T)) ∨ BitcopyType(T)
DropType(T)  ⇔ BuiltinDropType(T) ∨ HasDropMethod(StripPerm(T))

ImplementsEq(T)       ⇔ EqType(T) ∨ `Eq` ∈ Implements(T)
ImplementsHash(T)     ⇔ `Hash` ∈ Implements(T)
ImplementsIterator(T) ⇔ `Iterator` ∈ Implements(T)
ImplementsDiscrete(T) ⇔ BuiltinDiscreteType(T) ∨ `Discrete` ∈ Implements(T)
ImplementsHasher(T)   ⇔ `Hasher` ∈ Implements(T)

BuiltinDiscreteType(T) ⇔ StripPerm(T) = TypePrim(t) ∧ t ∈ IntTypes ∪ UnsignedIntTypes ∪ {`char`}
```

#### 14.10.3 Static Semantics

`Bitcopy`, `Clone`, `Drop`, and `FfiSafe` bounds are interpreted by **intrinsic satisfaction judgments**, not by user-defined class-implementation lookup. `Eq` is satisfied intrinsically when `EqType(T)` holds; `Discrete` intrinsically when `BuiltinDiscreteType(T)` holds. Other `Eq` and `Discrete` obligations are discharged through ordinary class implementation lookup.

**Bitcopy / Drop mutual exclusion.** A type may not be both bit-copyable and droppable:

```text
(BitcopyDrop-Ok)
¬(BitcopyType(T) ∧ DropType(T))
───────────────────────────────
Γ ⊢ T : BitcopyDropOk

(BitcopyDrop-Conflict)
BitcopyType(T) ∧ DropType(T)
─────────────────────────────
Γ ⊢ T : BitcopyDropOk ⇑
```

Violating this is `E-TYP-2621` ("Type satisfies both `BitcopyType` and `DropType`").

**`BitcopyType` is structural.** A `unique`-permission type is never `Bitcopy`. Otherwise it strips non-`unique` permissions and recurses; built-in `Bitcopy` types, tuples, fixed-length arrays, unions, records, enums, and modal states are `Bitcopy` exactly when every constituent is:

```text
BitcopyTypeCore(T) ⇔
  false                                          if T = TypePerm(`unique`, _)
  BitcopyTypeCore(T_0)                           if T = TypePerm(p, T_0) ∧ p ≠ `unique`
  BuiltinBitcopyType(T) ∨
  (T = TypeTuple([T_1, …, T_n]) ∧ ∀ i, BitcopyType(T_i)) ∨
  (T = TypeArray(T_0, e) ∧ Γ ⊢ ConstLen(e) ⇓ _ ∧ BitcopyType(T_0)) ∨
  (T = TypeUnion([T_1, …, T_n]) ∧ ∀ i, BitcopyType(T_i)) ∨
  (T = TypePath(p) ∧ RecordDecl(p) = R ∧ ∀ f : T_f ∈ Fields(R). BitcopyType(T_f)) ∨
  (T = TypePath(p) ∧ EnumDecl(p) = E ∧ ∀ v ∈ Variants(E). ∀ T_f ∈ PayloadTypes(v). BitcopyType(T_f)) ∨
  (T = TypeModalState(modal_ref, S) ∧ … ∀ T_f ∈ ModalPayload(modal_ref, S). BitcopyType(T_f)) ∨
  (T = ModalRefType(modal_ref) ∧ … ∀ S ∈ States(M). ∀ T_f ∈ ModalPayload(modal_ref, S). BitcopyType(T_f))
```

A `Bitcopy` record/enum with a non-`Bitcopy` field is `E-TYP-2622`. The built-in `Bitcopy` set (`BuiltinBitcopyType`) includes all `TypePrim`, safe and raw pointers, slices, function types, **every `$Class` dynamic type** (`TypeDynamic(Cl)`), all range forms over `Bitcopy` element types and `TypeRangeFull`, `string@View`, `bytes@View`, `FileKind`, `IoError`, and `Context`. The built-in `Drop` set is exactly the owning string/bytes: `BuiltinDropType(T) ⇔ T = string@Managed ∨ T = bytes@Managed`. And `BuiltinCloneType(T) ⇔ BuiltinBitcopyType(T)`.

**Built-in class signatures.** Using receiver shorthand (`~` = `const`, `~!` = `unique`):

- `Eq`: `eq(~, other: const Self) -> bool`
- `Hasher`: `write(~!, data: bytes@View) -> ()`; `finish(~) -> u64`
- `Hash`: `hash(~, hasher: unique Hasher) -> ()`
- `Iterator`: associated type `Item`; `next(~!) -> Self::Item | ()`
- `Discrete`: `successor(~) -> Self | ()`; `predecessor(~) -> Self | ()`

`Eq::eq` MUST be reflexive, symmetric, and transitive. `Hash` implementations MUST also implement `Eq`, and equal values MUST produce equal hash results from identical initial hasher states. `Iterator::next` returns `Self::Item` while iteration remains, or `()` when exhausted. `Discrete::successor`/`predecessor` define a discrete stepping relation and are partial inverses where both are defined.

#### 14.10.4 Dynamic Semantics

At the final owning scope exit, `drop` is invoked when `DropType(T)` holds, owned children are cleaned in **reverse construction order**, and the provenance/allocation domain is released. A moved-out binding transferred its domain and is skipped at its original scope exit. For types without `drop`, no type-specific destructor runs; domain release still occurs, and cleanup is a no-op when the value has no owned children and no domain storage to release.

`copy e` is the explicit object-duplication operation. It requires `BitcopyType(ExprType(e))`, duplicates the object bits, and materializes a fresh provenance/allocation domain for the duplicate; the original's cleanup responsibility stays with the original owner. `clone` on a `BitcopyType` value is equivalent to `copy` for the value-level duplication it performs.

`Hasher` keeps an internal `u64` state; `write` appends bytes to the input stream; `finish` returns the **FNV-1a 64-bit** hash of the concatenated byte stream using `FNVOffset64` and `FNVPrime64`. For integer `Discrete` types, `successor` returns the least representable value greater than the receiver (or `()` when none exists), and `predecessor` the greatest representable value smaller (or `()`); for `char`, the next/previous Unicode scalar value.

#### 14.10.5 Lowering

`Eq::eq` on `EqType(T)` lowers intrinsically to the built-in equality relation; `Discrete::successor`/`predecessor` on `BuiltinDiscreteType(T)` lower intrinsically to the built-in stepping relation; other `Eq`/`Discrete` calls lower through ordinary method-call lowering. These foundational class relations introduce no separate representation; they influence lowering through copy semantics, drop-glue generation, built-in call selection, and whether a dynamic-class vtable header carries a non-null drop entry.

#### 14.10.6 Worked Examples

**A `Bitcopy` record and explicit `copy`:**

```ultraviolet
/// All fields are `Bitcopy`, so `Point` is `Bitcopy` and never `Drop`.
public record Point {
    public x: i64
    public y: i64
}

public procedure duplicate(p: const Point) -> Point {
    return copy p
}
```

`Point` is `BitcopyType` because both fields are `i64` (built-in `Bitcopy`); `copy p` is admitted because `BitcopyType(Point)` holds.

**A `Drop` type with the exact `drop` signature** (`~!` = `unique` receiver, `()` return):

```ultraviolet
/// Owns a managed buffer; running `drop` releases it. Because it satisfies
/// `DropType`, it must NOT also satisfy `BitcopyType` — and it does not, because
/// `string@Managed` is a built-in `Drop` type, not `Bitcopy`.
public record OwnedText {
    public text: string@Managed

    public procedure drop(~!) -> () {
        return ()
    }
}
```

The `drop` method matches `Sig_T(T, m) = ⟨unique T, [], ()⟩`, so `HasDropMethod(OwnedText)` holds. `OwnedText` is not `Bitcopy` (its `string@Managed` field is a built-in `Drop` type), so **(BitcopyDrop-Ok)** is satisfied and there is no `E-TYP-2621` conflict.

**A foundational class bound on a generic alias:**

```ultraviolet
/// `Pair<TValue>` is well-formed only for `Bitcopy` element types.
public type Pair<TValue <: Bitcopy> = (TValue, TValue)
```

The `<TValue <: Bitcopy>` bound constrains instantiation: substituting a non-`Bitcopy` argument fails the bound (`E-TYP-2302` / `E-TYP-2530`).

### 14.11 Refinement and Polymorphism Diagnostics Supplement

This section owns the diagnostics for refinement types, generic instantiation, class implementation, dynamic objects, and foundational class requirements. The codes most directly relevant to this chapter:

**Refinement (§14.8):**

| Code | Severity | Detection | Condition |
| --- | --- | --- | --- |
| `E-TYP-1953` | Error | Compile-time | Refinement not provable outside `#dynamic` scope |
| `E-TYP-1954` | Error | Compile-time | Impure expression in refinement predicate |
| `E-TYP-1955` | Error | Compile-time | Predicate does not evaluate to `bool` |
| `E-TYP-1956` | Error | Compile-time | `self` used in inline parameter constraint |
| `E-TYP-1957` | Error | Compile-time | Circular type dependency in refinement predicate |
| `P-TYP-1953` | Panic | Runtime | Refinement predicate failed at runtime |

**Opaque types (§14.7):**

| Code | Severity | Detection | Condition |
| --- | --- | --- | --- |
| `E-TYP-2510` | Error | Compile-time | Accessing member not defined on opaque type's class |
| `E-TYP-2511` | Error | Compile-time | Opaque return type does not implement required class |
| `E-TYP-2512` | Error | Compile-time | Attempting to assign incompatible opaque types |

**Foundational classes (§14.10):**

| Code | Severity | Detection | Condition |
| --- | --- | --- | --- |
| `E-TYP-2621` | Error | Compile-time | Type satisfies both `BitcopyType` and `DropType` |
| `E-TYP-2622` | Error | Compile-time | `BitcopyType` has non-`BitcopyType` field |

**`#dynamic` placement (Chapter 9), relevant to refinement aliases:**

| Code | Severity | Detection | Condition |
| --- | --- | --- | --- |
| `E-CON-0410` | Error | Compile-time | `#dynamic` applied to contract clause directly |
| `E-CON-0411` | Error | Compile-time | `#dynamic` applied to type alias declaration |
| `E-CON-0412` | Error | Compile-time | `#dynamic` applied to field declaration |
| `W-CON-0401` | Warning | Compile-time | `#dynamic` present but all proofs succeed statically |

**Capability classes (§14.9)** surface through the dynamic-object diagnostics — `E-TYP-2540` (non-vtable-eligible procedure called on `$`), `E-TYP-2541` (dynamic class type created from non-dispatchable class), `E-TYP-2542` (generic procedure in class not vtable-eligible for `$` dispatch) — plus the `unsafe`-required allocation codes `AllocRaw-Unsafe-Err`/`DeallocRaw-Unsafe-Err` from §14.9.4. Generic and class-implementation codes in the same table that are relevant when capability or foundational classes are used as bounds include `E-TYP-2302`/`E-TYP-2530` (argument fails class bound), `E-TYP-2305` (class bound references a non-class type), `E-TYP-2503` (missing or mismatched required procedure), `E-TYP-2506` (duplicate class implementation), `E-TYP-2507` (orphan-rule violation), `E-TYP-2508` (cyclic superclass dependency), and `E-TYP-2509` (superclass bound refers to undefined class).

### Idioms & Best Practices

- **Hide representation with `opaque`, not with weak conventions.** When a module wants callers to depend only on a class interface and never on the concrete layout, return `opaque Cl`. This is enforced by **(T-Opaque-Project)** — callers physically cannot reach the hidden members — so you get a stable API surface for free. Keep the named class small and explicit (style guide: "Prefer narrow, specific APIs over broad convenience APIs").
- **Express invariants as refinements, not runtime guards.** Prefer `u32 |: { … }` over a hand-written check when the constraint is in the decidable fragment (literal comparisons, transitive integer inequalities, control-flow bounds, boolean combinations). The style guide is explicit: "Use the type system, `modal` types, contracts, invariants, and narrow capabilities before reaching for weaker runtime-only validation," and "Prefer contracts over ad hoc runtime checks when the language can express the rule."
- **Use `self` only in standalone refinement aliases.** In an inline parameter constraint, name the parameter. Confusing the two is the single most common refinement error (`E-TYP-1956`).
- **Reserve `#dynamic` for genuinely dynamic refinements.** It converts a static proof obligation into a runtime panic check. The style guide warns: "Do not use `[[dynamic]]` to bypass correct static conformance" (the compiling token is `#dynamic`). If a static formulation exists, use it; the compiler warns with `W-CON-0401` when a `#dynamic` scope's proofs all succeed statically.
- **Keep authority narrow.** Pass `$IO`, not `Context`; pass `$MonotonicTime`, not `$Time`, when you only read a monotonic clock. The style guide makes this a design rule, not a cleanup pass: "Do not thread through broad 'god context' objects for convenience" and "Capability narrowing is part of API design, not an optional cleanup pass." When several capabilities genuinely travel together at a real boundary, define a narrow projected context record — it still satisfies `MainSigOk` via `ContextBundleType`.
- **Mint sub-capabilities to attenuate.** Use the built-in narrowing methods — `io~>restrict(path)`, `net~>restrict_to_host(host)`, `heap~>with_quota(size)`, `time~>monotonic()` — to hand downstream code a strictly weaker capability than you hold.
- **Let foundational class relations be structural.** Do not hand-write `clone` for a type whose fields are all `Bitcopy`; `clone` is already `copy` there. Reserve a user `drop` for types that own an external resource, and remember a `Drop` type can never be `Bitcopy`.
- **Keep raw allocation inside `unsafe` wrappers.** `alloc_raw`/`dealloc_raw` require `unsafe` (§14.9.4); wrap them in a safe API that re-establishes invariants, per the style guide's `unsafe` rules ("Wrap unsafe operations in safe APIs that re-establish project invariants").

### Pitfalls & Diagnostics

- **Reading hidden members of an opaque value.** `opaque Counter` exposes only `Counter`'s interface; `c.count` (a concrete field) is `E-TYP-2510`. Returning a body whose type does not implement the named class is `E-TYP-2511`; mixing two different opaque class paths in an assignment is `E-TYP-2512`.
- **`|:` is one operator.** The refinement operator `|:` is a single token in `OperatorSet`; it is not a union bar `|` adjacent to a colon. The refinement braces are mandatory: `T |: { P }`.
- **`var`, not `let mut`; `loop`, not `while`.** Ultraviolet has no `mut` keyword and no `while` keyword. Mutable bindings are declared with `var`; condition loops are written `loop <condition> { … }`. Code copied from Rust-shaped examples that uses `let mut` or `while` will not parse.
- **Impure or non-`bool` predicates.** A refinement predicate must be pure (`E-TYP-1954`) and of type `bool` (`E-TYP-1955`). Effects, capability calls, or side-effecting expressions inside the predicate are rejected.
- **Unprovable refinement outside `#dynamic`.** If the prover cannot discharge `P[e/self]` and you are not in a `#dynamic` scope, the program is ill-formed (`E-TYP-1953`). Inside `#dynamic`, the same failure inserts a runtime check that panics on violation (`P-TYP-1953`).
- **`#dynamic` cannot annotate the alias.** `#dynamic` applied directly to a type-alias declaration is `E-CON-0411` (and to a field, `E-CON-0412`; to a contract clause, `E-CON-0410`). Establish the `#dynamic` scope on an enclosing declaration or attributed expression instead.
- **Capability classhood is structural, never nominal.** Naming a class `IODevice` does not make it a capability — only a capability superclass via `<:` does (`CapClass`). Do not rely on names; do not expect attributes to confer capability status.
- **`alloc_raw`/`dealloc_raw` outside `unsafe`.** These two `$HeapAllocator` methods are the only built-in capability operations that require `unsafe`; calling them in safe context is `AllocRaw-Unsafe-Err` / `DeallocRaw-Unsafe-Err`.
- **Non-dispatchable capability classes.** A `$Cl` requires `Cl` to be dispatchable — every effective method must be vtable-eligible (have a receiver, no generic params, no `Self` in its signature). A user capability class with a generic method is not dispatchable: forming `$Cl` from it is `E-TYP-2541`, and calling a non-eligible method is `E-TYP-2540` / `E-TYP-2542`. (`Reactor`'s generic `run`/`register` are built-ins lowered to primitives, not a template for user capability methods you intend to expose via `$`.)
- **Bitcopy/Drop conflict and bad fields.** A type that both satisfies `BitcopyType` and has a `drop` method (or an owning-string/bytes field) is `E-TYP-2621`. A type declared or required `Bitcopy` that contains a non-`Bitcopy` field is `E-TYP-2622`.
- **No ambient authority.** There is no global `IO`. If `main`'s parameter omits a capability you need, you cannot obtain it later (NAA-1/NAA-2). Declare the capability in `main`'s context bundle and thread it explicitly; capabilities only ever originate from the `Context` produced at the entry procedure or a hosted session.

*Related chapters:* Classes (§14.3), Concrete Implementers (§14.4), Associated Types (§14.5), and Dynamic Class Objects (§14.6) for the `$Class` form; Chapter 9 for the `#dynamic` attribute; §6.1 for the full capability/authority model; §24.4 for `ContextInitSigma`/`HostSessionInitSigma` and program entry.
