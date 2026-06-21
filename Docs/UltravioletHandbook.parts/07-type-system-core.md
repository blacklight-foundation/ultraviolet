## 7. The Type System Core: Equivalence, Subtyping & Inference

This chapter is the developer's working reference for the four judgments at the heart of Ultraviolet's static typing: **type equivalence** (`≡`), **subtyping** (`<:`), **type inference** (the `⇒` / `⇐` / `Solve` triad over equality constraints), and the **metatheoretic guarantees** those judgments are designed to uphold. It closes with the **core type diagnostics** the type checker raises when these judgments fail.

These judgments are *shared infrastructure*. The specification deliberately keeps constraint generation **feature-local**: each feature chapter (records, modal types, closures, generics, async, and so on) owns the rules that *synthesize* and *check* its own expressions and that *emit* its own equality constraints. This chapter owns the relations and machinery those rules consume — equivalence, subtyping, the unifier, the substitution algebra, and the solver. When you read a `⇒` or `⇐` rule in another chapter, the `Solve`, `Unify`, `≡`, and `<:` it appeals to are defined *here*.

Throughout, `Γ` is the typing environment, `𝒯` is the set of well-formed types, and judgments are written `Γ ⊢ …`. Where inference is in play the environment is extended to `Γ; R; L` (`R` is the return/result context, `L` the loop context) and synthesis carries a trailing constraint set, written `Γ; R; L ⊢ e ⇒ T ⊣ C`.

Related chapters: the **Data Types** chapter (the census of `Type…` constructors and the `type` grammar), the **Lexical** chapter (literal tokenization and suffix stripping), the **Procedures & Methods** and statement chapters (`let`/`var` binding effects beyond typing), the **Generics** chapter (variance declarations and instantiation), the **Refinement & Capability** chapter (predicate well-formedness), the **Classes & Implementations** chapter (`widen`, niche compatibility), and the **Permissions** chapter (permission admissibility).

---

### 7.1 Type Equivalence (`≡`)

Type equivalence answers a single question: *do two type expressions denote the same type?* It is the relation the compiler uses wherever a type must match exactly rather than merely be acceptable. The judgment form is:

```text
Γ ⊢ T ≡ U
```

Equivalence is **structural** (two types are equivalent when their constructors and corresponding components are equivalent), with a small number of carefully bounded normalizations (alias expansion, union permutation, refinement conjunction folding). It is an **equivalence relation**: reflexive, symmetric, and transitive, by the closing rules below.

#### 7.1.1 The shape of the rules

Every equivalence rule decomposes a constructor into its components and requires the components to be equivalent. The leaf rule is the primitive rule:

```text
(T-Equiv-Prim)
T = TypePrim(n)    U = TypePrim(n)
──────────────────────────────────
Γ ⊢ T ≡ U
```

A `TypePrim(n)` is one of the primitive type names — the integer types `IntTypes = {i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize}`, the float types `FloatTypes = {f16, f32, f64}`, `bool`, `char`, the unit type `()`, and the never type `!`. Two primitives are equivalent exactly when their names are byte-identical.

The recursive rules follow the same pattern. Permission-qualified types are equivalent only when **both** the permission and the underlying type agree:

```text
(T-Equiv-Perm)
T = TypePerm(p, T_0)    U = TypePerm(p, U_0)    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

Note that the permission `p` must be *identical* on both sides — `const T` and `unique T` are never equivalent. (The permission qualifiers are `const`, `unique`, and `shared`.) Tuples, arrays, slices, ranges, pointers, and applied generics decompose componentwise:

```text
(T-Equiv-Tuple)
T = TypeTuple([T_1, …, T_n])    U = TypeTuple([U_1, …, U_n])    ∀ i, Γ ⊢ T_i ≡ U_i
─────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Array)
T = TypeArray(T_0, e_0)    U = TypeArray(U_0, e_1)
Γ ⊢ ConstLen(e_0) ⇓ n    Γ ⊢ ConstLen(e_1) ⇓ n    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Slice)
T = TypeSlice(T_0)    U = TypeSlice(U_0)    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Apply)
T = TypeApply(path, [T_1, …, T_n])    U = TypeApply(path, [U_1, …, U_n])    ∀ i, Γ ⊢ T_i ≡ U_i
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

The array rule is worth dwelling on: array lengths are **values**, not just syntax. Two array types are equivalent when their element types are equivalent **and** their length expressions evaluate (via `ConstLen`, §7.1.3) to the *same* natural number `n`. The two length expressions need not be syntactically identical — `[i32; 4]` and `[i32; 2 + 2]` are equivalent because both lengths evaluate to `4`.

The remaining structural rules cover the full constructor census. Function and closure types decompose over their (mode, type) parameter pairs and their result; the parameter *modes* `m_i` must match positionally:

```text
(T-Equiv-Func)
T = TypeFunc([⟨m_1, T_1⟩, …, ⟨m_n, T_n⟩], R)
U = TypeFunc([⟨m_1, U_1⟩, …, ⟨m_n, U_n⟩], S)    ∀ i, Γ ⊢ T_i ≡ U_i    Γ ⊢ R ≡ S
──────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Closure)
T = TypeClosure([⟨m_1, T_1⟩, …, ⟨m_n, T_n⟩], R, D)
U = TypeClosure([⟨m_1, U_1⟩, …, ⟨m_n, U_n⟩], S, D)    ∀ i, Γ ⊢ T_i ≡ U_i    Γ ⊢ R ≡ S
──────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

A closure type additionally carries a dependency descriptor `D` (the captured `shared` dependencies); it must be **identical** on both sides for the closure types to be equivalent.

Pointer, raw-pointer, dynamic, opaque, string, bytes, modal-state, and path types are equivalent only when their fixed discriminators match exactly (and their type arguments, where present, are equivalent):

```text
(T-Equiv-Ptr)
T = TypePtr(T_0, s)    U = TypePtr(U_0, s)    Γ ⊢ T_0 ≡ U_0
───────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-RawPtr)
T = TypeRawPtr(q, T_0)    U = TypeRawPtr(q, U_0)    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Path)
T = TypePath(p)    U = TypePath(p)
──────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-ModalState)
T = TypeModalState(modal_ref, S)    U = TypeModalState(modal_ref, S)
────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-String)
T = TypeString(st)    U = TypeString(st)
────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Bytes)
T = TypeBytes(st)    U = TypeBytes(st)
──────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Dynamic)
T = TypeDynamic(p)    U = TypeDynamic(p)
────────────────────────────────────────
Γ ⊢ T ≡ U

(T-Equiv-Opaque)
T = TypeOpaque(path)    U = TypeOpaque(path)
────────────────────────────────────────────
Γ ⊢ T ≡ U
```

For a safe pointer `TypePtr(T_0, s)` the pointer **state** `s` (one of `Valid`, `Null`, `Expired`, or the unrefined `⊥`) must match; for a raw pointer `TypeRawPtr(q, T_0)` the qualifier `q` (`imm` or `mut`) must match; for `string`/`bytes` the state `st` (`Managed`, `View`, or the unrefined `⊥` for a bare `string`/`bytes`) must match; for a modal-state type the *referenced modal* and the *state name* `S` must both match.

> **State discriminators are part of the type.** A bare `string` is `TypeString(⊥)`; a string literal has type `TypeString(@View)`; an open file handle might be `TypeString(@Managed)`. These three are pairwise *inequivalent* by `T-Equiv-String`, because the state discriminator differs. The same holds for `bytes` and for safe pointers. Do not assume a bare `string` and a `@View` string are the same type.

The full family of range types decomposes over the element type:

```text
(T-Equiv-Range)               T = TypeRange(T_0),               U = TypeRange(U_0),               require Γ ⊢ T_0 ≡ U_0
(T-Equiv-RangeInclusive)      T = TypeRangeInclusive(T_0),      U = TypeRangeInclusive(U_0),      require Γ ⊢ T_0 ≡ U_0
(T-Equiv-RangeFrom)           T = TypeRangeFrom(T_0),           U = TypeRangeFrom(U_0),           require Γ ⊢ T_0 ≡ U_0
(T-Equiv-RangeTo)             T = TypeRangeTo(T_0),             U = TypeRangeTo(U_0),             require Γ ⊢ T_0 ≡ U_0
(T-Equiv-RangeToInclusive)    T = TypeRangeToInclusive(T_0),    U = TypeRangeToInclusive(U_0),    require Γ ⊢ T_0 ≡ U_0
```

and the unbounded full range is equivalent only to itself:

```text
(T-Equiv-RangeFull)
T = TypeRangeFull    U = TypeRangeFull
──────────────────────────────────────
Γ ⊢ T ≡ U
```

#### 7.1.2 Unions: equivalence is permutation-closed

Union types are equivalence-checked **up to permutation** and **up to equivalence of members** — order is irrelevant, but membership is by `≡`, not by mere structural identity:

```text
MembersEq([T_1, …, T_n], [U_1, …, U_n]) ⇔
  ∃ U'. Permutation(U', [U_1, …, U_n]) ∧ ∀ i. 0 ≤ i < n ⇒ Γ ⊢ T_i ≡ U'[i]

(T-Equiv-Union)
T = TypeUnion([T_1, …, T_n])    U = TypeUnion([U_1, …, U_n])    MembersEq([T_1, …, T_n], [U_1, …, U_n])
─────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

So `i32 | bool` and `bool | i32` are equivalent. (Both unions must have the same arity `n` for the permutation to exist.) Be careful to distinguish this from how the **unifier** treats unions in §7.3, which is *member-syntactic* rather than permutation-of-`≡` — the two relations agree on closed unions but the unifier is the decidable approximation.

For canonical rendering and sorting, the spec fixes a deterministic key:

```text
TypeKeyString(T)        = TypeRender(AliasExpand(T))
UnionSort([T_1, …, T_n]) = the stable sort of [T_1, …, T_n] by byte-wise
                           lexicographic order of TypeKeyString
```

`UnionSort` is what gives unions a canonical written form (used by the unifier and by diagnostics); `AliasExpand` ensures alias names are resolved before the key is computed, so equivalence is *transparent through aliases*.

The union grammar requires at least two members: `union_type ::= non_union_type ("|" non_union_type)+`. A type alias may name a union, and alias expansion makes the alias equivalent to its body.

```ultraviolet
public type Token = i32 | bool

procedure firstField(pair: (i32, bool)) -> (i32, bool) {
    // The annotated parameter type and the synthesized tuple type are
    // checked with `T-Equiv-Tuple` componentwise.
    return pair
}

// `Token` and `bool | i32` are equivalent: alias expansion (AliasExpand)
// turns `Token` into `i32 | bool`, and `T-Equiv-Union` is permutation-closed.
let flag: Token = true
let mirror: bool | i32 = flag
```

#### 7.1.3 Array lengths: `ConstLen`

Array equivalence (and array subtyping and unification) all hinge on evaluating a length expression to a natural number. The judgment `Γ ⊢ ConstLen(e) ⇓ n` has three success rules and one error rule.

A literal length is read directly, provided it fits `usize`:

```text
(ConstLen-Lit)
e = Literal(lit)    lit.kind = IntLiteral    InRange(IntValue(lit), "usize")    n = IntValue(lit)
─────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ConstLen(e) ⇓ n
```

A path to a `let`/`var` static binding is followed to its initializer and that initializer's `ConstLen` is taken:

```text
(ConstLen-Path)
e = Path(path, name)    ValuePathType(path, name) = T
StaticDecl(_, _, _, ⟨IdentPattern(name), _, "=", init, _⟩, _, _) ∈ Γ    Γ ⊢ ConstLen(init) ⇓ n
─────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ConstLen(e) ⇓ n
```

Any other length expression is evaluated by the **pure compile-time evaluator**, with *no capabilities and no emission rights*:

```text
CtPureEnv(Γ) = the compile-time environment of §22.1.5 with no capability
               bindings and no emission rights
Φ_pure       = ⟨files: ∅, root: ⊥, [], [], 0⟩

(ConstLen-Comptime)
e does not match the Literal form of ConstLen-Lit
e does not match the Path form of ConstLen-Path
Γ ⊢ CtEval(CtPureEnv(Γ), Φ_pure, e) ⇓ (CtPrim(n), _, Φ_1)
CtPendingEmits(Φ_1) = []    n ∈ ℕ    InRange(n, "usize")
─────────────────────────────────────────────────────────
Γ ⊢ ConstLen(e) ⇓ n
```

Array-length expressions therefore admit *any pure, capability-free, emission-free compile-time-evaluable expression*. This evaluation runs in Phase 3 over the Phase-2-expanded module set; any compile-time builtin call that requires capabilities or would emit code leaves `CtEval` undefined, so the error rule fires:

```text
(ConstLen-Err)
¬ ∃ n. Γ ⊢ ConstLen(e) ⇓ n    c = Code(ConstLen-Err)
─────────────────────────────────────────────────────
Γ ⊢ ConstLen(e) ⇑ c
```

```ultraviolet
public let LANE_COUNT: usize = 8

// All three array types below are mutually equivalent by T-Equiv-Array:
//   ConstLen(8)            ⇓ 8  via ConstLen-Lit
//   ConstLen(LANE_COUNT)   ⇓ 8  via ConstLen-Path  (follows the static init)
//   ConstLen(4 + 4)        ⇓ 8  via ConstLen-Comptime (pure const-eval)
let direct: [f32; 8] = [0.0; 8]
let by_name: [f32; LANE_COUNT] = direct
let by_expr: [f32; 4 + 4] = by_name
```

#### 7.1.4 Refinements: subject-normalized predicate equality

Refinement types `TypeRefine(T_0, P)` attach a predicate `P` to a base type. Two refinements are equivalent when their bases are equivalent **and** their predicates are equivalent after normalizing away the binder name:

```text
PredSubject(P) = the binder identifier the refinement form introduces for the
                 refined value in P
PredNorm(P)    = P with every free occurrence of PredSubject(P) replaced by the
                 reserved subject symbol ⌀, and no other rewriting
PredicateEquiv(P_1, P_2) ⇔ PredNorm(P_1) = PredNorm(P_2)   (structural AST equality)

(T-Equiv-Refine)
T = TypeRefine(T_0, P_1)    U = TypeRefine(U_0, P_2)    Γ ⊢ T_0 ≡ U_0    PredicateEquiv(P_1, P_2)
────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

This is a deliberately **conservative** rule. Predicate equivalence is *structural AST equality after subject renaming* — nothing more. Predicates that are semantically equivalent but structurally distinct (for example `self > 0` versus `0 < self`, or `self >= 1`) are **not** equivalent for type equivalence. The spec is explicit that implementations **MUST NOT** accept broader equivalences; this keeps `≡` decidable and keeps it aligned with the corresponding unifier rule `Unify-Refine`.

The subject binder depends on the refinement form. A **standalone refinement type** — a `type` alias of the form `type Name = Base |: { predicate }` — introduces the constrained value as `self` (the `predicate` must reference the constrained value as `self` and must be pure). A **parameter refinement** — a `refinement_clause` written inline on a parameter's type — references the parameter by name and **MUST NOT** use `self`. `PredNorm` exists precisely so that two refinements whose binders are *different identifiers* (e.g. two parameter refinements named after different parameters) can still be equivalent: it rewrites each refinement's own subject to the reserved symbol `⌀` before comparing.

Nested refinements normalize to a single conjoined predicate:

```text
(T-Equiv-Refine-Norm)
T = TypeRefine(TypeRefine(T_0, P_1), P_2)    U = TypeRefine(T_0, P_1 ∧ P_2)
───────────────────────────────────────────────────────────────────────────
Γ ⊢ T ≡ U
```

```ultraviolet
// Standalone refinement types: the constrained value is always `self`.
// `i32 |: { self > 0 }` written two ways with the SAME predicate text is
// equivalent. PredNorm normalizes `self` to the reserved subject symbol ⌀.
public type Positive = i32 |: { self > 0 }
public type Strictly = i32 |: { self > 0 }

let radius: Positive = 5        // T-Refine-Intro: proof obligation `5 > 0`
let span: Strictly = radius     // Positive <: Strictly: identical normalized predicate
```

#### 7.1.5 The equivalence-relation closure

Finally, `≡` is closed as a genuine equivalence relation:

```text
(T-Equiv-Refl)   T ∈ 𝒯  ⊢  Γ ⊢ T ≡ T
(T-Equiv-Sym)    Γ ⊢ T ≡ U  ⊢  Γ ⊢ U ≡ T
(T-Equiv-Trans)  Γ ⊢ T ≡ U,  Γ ⊢ U ≡ V  ⊢  Γ ⊢ T ≡ V
```

Reflexivity covers any constructor not otherwise decomposed (it is what makes, e.g., two textually identical opaque or path types trivially equivalent even before the specific rule applies); symmetry and transitivity let the checker chain equivalences and freely orient a comparison.

---

### 7.2 Subtyping (`<:`)

Subtyping is the relation `Γ ⊢ T <: U` — "a value of type `T` may be used where a `U` is expected." It is **where coercion happens**: the single place in the language that admits a value at a *different* (more specific) type than the one declared. It is consumed almost entirely through one rule, the subsumption rule of inference (`Chk-Subsumption`, §7.3.6): when an expression synthesizes type `S` and the context expects `T`, the program type-checks iff `Γ ⊢ S <: T`.

Subtyping is **reflexive and transitive** but, unlike `≡`, *not* symmetric — that asymmetry is the entire point.

#### 7.2.1 Where subtyping does *not* widen: numerics and permissions

Two foundational non-subtyping facts bound the relation:

```text
∀ T, U ∈ IntTypes.   T ≠ U ⇒ ¬(Γ ⊢ T <: U)
∀ T, U ∈ FloatTypes. T ≠ U ⇒ ¬(Γ ⊢ T <: U)
```

There is **no implicit numeric widening**. `i32` is not a subtype of `i64`; `f32` is not a subtype of `f64`. Distinct numeric types only relate by reflexivity. To change a numeric type you must use an explicit `as` cast or `widen` expression (see the **Casts & Conversions** chapter). This is a frequent surprise for developers arriving from languages with implicit promotion.

Permissions likewise do **not** widen through general subtyping. The spec defers permission *admissibility* to the Permissions chapter and states here that, for type subtyping, **permission equality is required**:

```text
(Sub-Perm)
T = TypePerm(p, T_0)    U = TypePerm(q, U_0)    p = q    Γ ⊢ T_0 <: U_0
───────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

The permission qualifiers `p` and `q` must be equal; only the underlying types are related by `<:`. Permission admissibility at receiver/argument positions (a separate relation, `PermAdmits`) is owned by the Permissions chapter and must not rewrite the static type of a caller expression.

#### 7.2.2 The bottom type

The never type is a subtype of *everything*:

```text
(Sub-Never)
T ∈ 𝒯
─────────────────────────────
Γ ⊢ TypePrim("!") <: T
```

This is what lets a diverging expression (a `return`, a `loop` that never breaks, a call that never returns) be used in any typed position — its `!` type subsumes into whatever the context demands.

#### 7.2.3 Structural (covariant) subtyping

For the data constructors, subtyping is **covariant** componentwise — the container is a subtype when its components are subtypes:

```text
(Sub-Tuple)
T = TypeTuple([T_1, …, T_n])    U = TypeTuple([U_1, …, U_n])    ∀ i, Γ ⊢ T_i <: U_i
─────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U

(Sub-Array)
T = TypeArray(T_0, e_0)    U = TypeArray(U_0, e_1)
Γ ⊢ ConstLen(e_0) ⇓ n    Γ ⊢ ConstLen(e_1) ⇓ n    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────────────
Γ ⊢ T <: U

(Sub-Slice)
T = TypeSlice(T_0)    U = TypeSlice(U_0)    Γ ⊢ T_0 ≡ U_0
─────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

Read these carefully: tuples are *deeply covariant* (`Sub-Tuple` recurses with `<:`), but **arrays and slices are invariant in their element type** — they require element *equivalence* (`≡`), with arrays additionally requiring equal lengths. The reason is mutability: an array or slice is an addressable, writable container, so allowing a covariant element subtype would be unsound. The range constructors, by contrast, are covariant (a range is read-only over its bound type):

```text
(Sub-Range)              T = TypeRange(T_0),            U = TypeRange(U_0),            require Γ ⊢ T_0 <: U_0
(Sub-RangeInclusive)     T = TypeRangeInclusive(T_0),   U = TypeRangeInclusive(U_0),   require Γ ⊢ T_0 <: U_0
(Sub-RangeFrom)          T = TypeRangeFrom(T_0),        U = TypeRangeFrom(U_0),        require Γ ⊢ T_0 <: U_0
(Sub-RangeTo)            T = TypeRangeTo(T_0),          U = TypeRangeTo(U_0),          require Γ ⊢ T_0 <: U_0
(Sub-RangeToInclusive)   T = TypeRangeToInclusive(T_0), U = TypeRangeToInclusive(U_0), require Γ ⊢ T_0 <: U_0

(Sub-RangeFull)
T = TypeRangeFull    U = TypeRangeFull
──────────────────────────────────────
Γ ⊢ T <: U
```

#### 7.2.4 Pointer and modal subtyping

A safe pointer in a *known* `Valid` or `Null` state subsumes into the unstated (`⊥`) state, which models "state-erased" — you may always forget what you knew about a pointer's state:

```text
(Sub-Ptr-State)
s ∈ {Valid, Null}
─────────────────────────────────────
Γ ⊢ TypePtr(T, s) <: TypePtr(T, ⊥)
```

Note that `Expired` is *not* in the subsumption set: a `@Expired` pointer does not silently erase to the unstated pointer.

A modal value in a specific state subsumes into the *general* modal type **only when the state is niche-compatible** with the general representation:

```text
(Sub-Modal-Niche)
ModalDeclOf(modal_ref) = M    S ∈ States(M)    NicheCompatible(modal_ref, S)
─────────────────────────────────────────────────────────────────────────────
Γ ⊢ TypeModalState(modal_ref, S) <: ModalRefType(modal_ref)
```

`NicheCompatible(modal_ref, S)` holds when the modal declaration has a niche and the state-specific type is representation-preserving against the general modal type. When a state is *not* niche-compatible, subsumption to the general type is **not** a `<:` step; it must be done explicitly with `widen` (see the **Classes & Implementations** chapter), and a non-niche checked subsumption raises `Chk-Subsumption-Modal-NonNiche` (§7.3.6).

#### 7.2.5 Functions, closures, and async: contravariant parameters

Function and closure subtyping is the classic rule: **covariant in the result, contravariant in the parameters**. A function is a subtype of another when it accepts *more* (parameters subtype the other way) and returns *less* (results subtype the same way):

```text
(Sub-Func)
T = TypeFunc([⟨m_1, T_1⟩, …, ⟨m_n, T_n⟩], R)
U = TypeFunc([⟨m_1, U_1⟩, …, ⟨m_n, U_n⟩], S)    ∀ i, Γ ⊢ U_i <: T_i    Γ ⊢ R <: S
──────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U

(Sub-Closure)
T = TypeClosure([⟨m_1, T_1⟩, …, ⟨m_n, T_n⟩], R, D)
U = TypeClosure([⟨m_1, U_1⟩, …, ⟨m_n, U_n⟩], S, D)    ∀ i, Γ ⊢ U_i <: T_i    Γ ⊢ R <: S
─────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

Note the flipped direction on the parameters (`U_i <: T_i`) versus the result (`R <: S`). Parameter *modes* `m_i` and (for closures) the dependency descriptor `D` must match. Async types follow the analogous variance over their four-part signature — output and result covariant, input contravariant, error covariant:

```text
(Sub-Async)
AsyncSig(T) = ⟨Out_1, In_1, Result_1, E_1⟩    AsyncSig(U) = ⟨Out_2, In_2, Result_2, E_2⟩
Γ ⊢ Out_1 <: Out_2    Γ ⊢ In_2 <: In_1    Γ ⊢ Result_1 <: Result_2    Γ ⊢ E_1 <: E_2
─────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

#### 7.2.6 Union subtyping: membership and width

A type is a subtype of a union when it is equivalent to one of the union's members, and one union is a subtype of another when *every* member of the first is a member of the second:

```text
Member(T, U) ⇔ U = TypeUnion([U_1, …, U_n]) ∧ ∃ i. Γ ⊢ T ≡ U_i

(Sub-Member-Union)
Member(T, U)
─────────────────
Γ ⊢ T <: U

(Sub-Union-Width)
U_1 = TypeUnion([T_1, …, T_n])    U_2 = TypeUnion([U_1', …, U_m'])    ∀ i, Member(T_i, U_2)
───────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ U_1 <: U_2
```

`Member` is defined by `≡` (which is permutation-closed), so membership and width are insensitive to union ordering. `Sub-Union-Width` is what lets a narrower union flow into a wider one. Because `Member` uses `≡`, a candidate type must be *equivalent* to a union member — including matching any state discriminator. (A `@View` string is not a member of a union whose member is a bare `string`, because `TypeString(@View) ≢ TypeString(⊥)`.)

```ultraviolet
public type Small = i32 | bool
public type Wide = i32 | bool | char

procedure accept(value: Wide) -> Wide {
    return value
}

// Sub-Member-Union: `i32 <: (i32 | bool | char)`
let single: Wide = 7
// Sub-Union-Width: every member of `Small` is a member of `Wide`,
// so `Small <: Wide` and the value subsumes on the way in.
let narrow: Small = true
let widened: Wide = narrow
```

#### 7.2.7 Generic subtyping and declared variance

For an applied generic `TypeApply(path, args)`, subtyping is governed by the **per-parameter variance** declared on the generic:

```text
Variance = {+, -, =, ±}
VarianceOf(path, i) = v ⇔ GenericDecl(path) = ⟨params, _⟩ ∧ params[i].variance = v

VarianceSatisfied(v, T, U) ⇔
  (v = + ∧ Γ ⊢ T <: U) ∨      // covariant
  (v = - ∧ Γ ⊢ U <: T) ∨      // contravariant
  (v = = ∧ Γ ⊢ T ≡ U) ∨       // invariant
  (v = ±)                      // bivariant (anything goes)

(Sub-Generic)
T = TypeApply(path, [T_1, …, T_n])    U = TypeApply(path, [U_1, …, U_n])
∀ i, VarianceSatisfied(VarianceOf(path, i), T_i, U_i)
─────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

A covariant parameter (`+`) requires `T_i <: U_i`; contravariant (`-`) flips it; invariant (`=`) demands equivalence; bivariant (`±`) imposes nothing. When `VarianceSatisfied` fails, `Sub-Generic` is simply *inapplicable* — there is no subtype relationship. Diagnostics for variance violations are **not** raised by the relation itself; they are emitted at the *use site* that required the subtype relationship (the checked-subsumption variance diagnostic `Chk-Generic-Variance-Err` in the Generics chapter), so that the error points at the actual mismatch in the program rather than at the abstract relation.

#### 7.2.8 Reflexivity and transitivity

```text
(Sub-Refl)   T ∈ 𝒯  ⊢  Γ ⊢ T <: T
(Sub-Trans)  Γ ⊢ T <: U,  Γ ⊢ U <: V  ⊢  Γ ⊢ T <: V
```

Reflexivity guarantees that an exact-type assignment always type-checks (every `T` is acceptable where `T` is expected); transitivity lets the checker compose subtyping through intermediate types — for example, a niche modal state into the general modal type and then that general type into a union containing it.

---

### 7.3 Type Inference

Inference in Ultraviolet is **constraint-based**: feature rules synthesize (`⇒`) and check (`⇐`) expressions, accumulating a set of *equality constraints*; a unifier discharges that set into a substitution; and the substitution is applied to recover concrete types. This section owns the shared machinery — the constraint domain, substitutions, the unifier, the solver, and the bidirectional `⇒`/`⇐` framework — that every feature chapter's rules consume.

```text
TypeInfJudg = {⇒, ⇐, Solve}

Constraint    = Type × Type
ConstraintSet = ℘(Constraint)
```

Constraint generation is **feature-local**. This chapter defines only the shared equality-constraint domain, the substitution machinery, and the solver. Rules that generate no additional equalities yield `∅`.

#### 7.3.1 Type variables and substitutions

```text
TVar     = {α, β, γ, ...}
TVars(T) = set of type variables occurring in T

Subst  = TVar ⇀ Type
Dom(θ) = {α | θ(α) defined}
Id     = ∅
```

A substitution `θ` maps type variables to types and is applied homomorphically across the constructor census. The defining clauses:

```text
θ(TypePrim(p))           = TypePrim(p)
θ(TVar(α))               = θ(α) if α ∈ Dom(θ), else TVar(α)
θ(TypeTuple(Ts))         = TypeTuple([θ(T) | T ∈ Ts])
θ(TypeArray(T, n))       = TypeArray(θ(T), n)
θ(TypeSlice(T))          = TypeSlice(θ(T))
θ(TypeUnion(Ts))         = TypeUnion([θ(T) | T ∈ Ts])
θ(TypeFunc(ps, R))       = TypeFunc([(m, θ(T)) | (m, T) ∈ ps], θ(R))
θ(TypePtr(T, s))         = TypePtr(θ(T), s)
θ(TypePerm(p, T))        = TypePerm(p, θ(T))
θ(TypeApply(path, args)) = TypeApply(path, [θ(T) | T ∈ args])
θ distributes over all remaining type constructors.
```

Note that `θ` leaves array *lengths* untouched (`TypeArray(T, n)` substitutes only the element type) and leaves the pointer state, permission, and path discriminators fixed. Substitution composition is left-to-right function composition:

```text
θ₁ ∘ θ₂ = λα. θ₁(θ₂(α))
```

#### 7.3.2 The unifier

Unification is a small-step state machine over a worklist of constraints paired with the substitution accumulated so far:

```text
UnifyState = {UnifyStart(C), UnifyStep(C, θ), UnifyDone(θ), UnifyFail}
```

It begins with an empty substitution and terminates either in `UnifyDone(θ)` (success, with the most-general unifier `θ`) or `UnifyFail`.

**Base case.** An empty constraint set yields the identity substitution:

```text
(Unify-Empty)
──────────────────────────────────────
⟨UnifyStart(∅)⟩ → ⟨UnifyDone(Id)⟩
```

**Trivial equality.** Syntactically equal sides are discharged with no effect:

```text
(Unify-Eq)
T = U
───────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep(C, θ)⟩
```

**Variable elimination (with the occurs check).** When one side is a type variable not occurring in the other, the variable is solved and the binding is propagated into both the remaining constraints and the accumulated substitution:

```text
(Unify-Var-L)
T = TVar(α)    α ∉ TVars(U)
──────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep([α ↦ U]C, [α ↦ U] ∘ θ)⟩

(Unify-Var-R)
U = TVar(α)    α ∉ TVars(T)
──────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep([α ↦ T]C, [α ↦ T] ∘ θ)⟩
```

If the variable *does* occur in the other side and the two are not equal, unification fails — this is the **occurs check**, which prevents infinite types:

```text
(Unify-Occurs-Fail)
T = TVar(α)    α ∈ TVars(U)    T ≠ U
──────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

**Structural decomposition.** Each composite constructor decomposes a constraint into constraints on its components (and fails on arity/shape mismatch). The decomposition rules:

```text
(Unify-Tuple)        tuples → componentwise constraints  (Unify-Tuple-Fail on n ≠ m)
(Unify-Array)        arrays → element constraint, when ConstLen agrees on both sides
                              (Unify-Array-Len-Fail when the constant lengths differ)
(Unify-Slice)        slices → element constraint
(Unify-Perm)         perms with equal p → underlying constraint  (Unify-Perm-Fail on p ≠ q)
(Unify-Func)         functions → parameter + result constraints  (Unify-Func-Fail on arity/mode)
(Unify-Closure)      closures → parameter + result constraints
                              (Unify-Closure-Fail on D mismatch or arity/mode)
(Unify-Ptr)          pointers with equal state → underlying constraint
                              (Unify-Ptr-State-Fail on s ≠ s')
(Unify-RawPtr)       raw pointers with equal qualifier → underlying constraint
                              (Unify-RawPtr-Qual-Fail on q ≠ q')
(Unify-Apply)        applied generics with equal path/arity → argument constraints
                              (Unify-Apply-Fail on path or arity mismatch)
(Unify-Range…)       each range constructor → element constraint
```

Spelling out two representative decompositions and one failure:

```text
(Unify-Tuple)
T = TypeTuple([T_1, …, T_n])    U = TypeTuple([U_1, …, U_n])
──────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep({(T_1, U_1), …, (T_n, U_n)} ∪ C, θ)⟩

(Unify-Func)
T = TypeFunc([(m_1, T_1), …, (m_n, T_n)], R_T)
U = TypeFunc([(m_1, U_1), …, (m_n, U_n)], R_U)
──────────────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep({(T_1, U_1), …, (T_n, U_n), (R_T, R_U)} ∪ C, θ)⟩

(Unify-Array-Len-Fail)
T = TypeArray(T_0, e_0)    U = TypeArray(U_0, e_1)
Γ ⊢ ConstLen(e_0) ⇓ n_0    Γ ⊢ ConstLen(e_1) ⇓ n_1    n_0 ≠ n_1
─────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

**Refinements.** Unification of refinements requires the *same* subject-normalized predicate (the same conservative rule as `T-Equiv-Refine`) and then unifies the bases:

```text
(Unify-Refine)
T = TypeRefine(T_0, pred_T)    U = TypeRefine(U_0, pred_U)    PredNorm(pred_T) = PredNorm(pred_U)
──────────────────────────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep({(T_0, U_0)} ∪ C, θ)⟩

(Unify-Refine-Pred-Fail)
T = TypeRefine(T_0, pred_T)    U = TypeRefine(U_0, pred_U)    PredNorm(pred_T) ≠ PredNorm(pred_U)
──────────────────────────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

**Primitives.** Distinct primitive names never unify:

```text
(Unify-Prim-Fail)
T = TypePrim(p_T)    U = TypePrim(p_U)    p_T ≠ p_U
────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

**Unions in unification are member-syntactic, not permutation-of-`≡`.** Two unions unify only when *both are closed* (contain no type variables) and their `UnionSort` canonical forms are byte-identical:

```text
(Unify-Union-Eq)
T = TypeUnion(Ts)    U = TypeUnion(Us)
TVars(T) = ∅    TVars(U) = ∅    UnionSort(Ts) = UnionSort(Us)
──────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep(C, θ)⟩

(Unify-Union-Fail)
T = TypeUnion(Ts)    U = TypeUnion(Us)
(TVars(T) ≠ ∅ ∨ TVars(U) ≠ ∅ ∨ UnionSort(Ts) ≠ UnionSort(Us))
──────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

The spec is explicit about the relationship between the two relations: unification over unions is *order-insensitive but member-syntactic*; unions containing unsolved type variables do **not** unify. `T-Equiv-Union` remains permutation-closed over `≡` and *strictly contains* the solver's relation — **the solver is the decidable approximation of equivalence.** In practice this means: rely on `≡`/subsumption to relate unions you have written out by hand, but do not expect inference to *solve a variable through* a union.

**Rigid heads.** Path, string, bytes, dynamic, opaque, modal-state, and full-range types are rigid — distinct ones fail directly:

```text
(Unify-Rigid-Fail)
((T = TypePath(_) ∧ U = TypePath(_)) ∨
 (T = TypeString(_) ∧ U = TypeString(_)) ∨
 (T = TypeBytes(_) ∧ U = TypeBytes(_)) ∨
 (T = TypeDynamic(_) ∧ U = TypeDynamic(_)) ∨
 (T = TypeOpaque(_) ∧ U = TypeOpaque(_)) ∨
 (T = TypeModalState(_, _) ∧ U = TypeModalState(_, _)) ∨
 (T = TypeRangeFull ∧ U = TypeRangeFull))    T ≠ U
────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

**Head-constructor mismatch.** When the two sides have different head constructors and neither is a variable, unification fails. The head-constructor discriminator (distinct from the deep constructor census `TypeCtor`) is:

```text
HeadCtor(TypePrim(n)) = n               HeadCtor(TypeString(_)) = string
HeadCtor(TypeTuple(_)) = tuple          HeadCtor(TypeBytes(_)) = bytes
HeadCtor(TypeArray(_, _)) = array       HeadCtor(TypeDynamic(p)) = ⟨dyn_class, p⟩
HeadCtor(TypeSlice(_)) = slice          HeadCtor(TypeOpaque(p)) = ⟨opaque, p⟩
HeadCtor(TypeUnion(_)) = union          HeadCtor(TypeRefine(_, _)) = refinement
HeadCtor(TypeFunc(_, _)) = function     HeadCtor(TypeModalState(_, _)) = modal_state
HeadCtor(TypeClosure(_, _, _)) = closure   HeadCtor(TypeRange(_)) = range
HeadCtor(TypePtr(_, _)) = ptr           HeadCtor(TypeRangeInclusive(_)) = range_inclusive
HeadCtor(TypeRawPtr(_, _)) = rawptr     HeadCtor(TypeRangeFrom(_)) = range_from
HeadCtor(TypePerm(_, _)) = perm         HeadCtor(TypeRangeTo(_)) = range_to
HeadCtor(TypeRangeToInclusive(_)) = range_to_inclusive
HeadCtor(TypeRangeFull) = range_full
HeadCtor(TypeApply(p, _)) = ⟨apply, p⟩  HeadCtor(TypePath(p)) = ⟨path, p⟩

(Unify-Ctor-Mismatch)
HeadCtor(T) ≠ HeadCtor(U)    T ∉ TVar    U ∉ TVar
──────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyFail⟩
```

A decomposition rule and a failure rule **never apply to the same constraint pair**: the failure rules apply only when no decomposition rule and no variable rule applies. This makes the machine deterministic on the choice of *whether* a pair succeeds or fails.

**Lifting the machine to a judgment.** The reflexive-transitive closure of the step relation lifts to the `Unify` judgment, which either yields a substitution or raises the unification error code:

```text
(Unify-Ok)
⟨UnifyStart(C)⟩ →* ⟨UnifyDone(θ)⟩
──────────────────────────────────
Γ ⊢ Unify(C) ⇓ θ

(Unify-Err)
⟨UnifyStart(C)⟩ →* ⟨UnifyFail⟩    c = Code(Unify-Fail)
───────────────────────────────────────────────────────
Γ ⊢ Unify(C) ⇑ c
```

#### 7.3.3 The solver

The solver is a thin wrapper that runs the unifier over a constraint set:

```text
(Solve-Unify)
Γ ⊢ Unify(C) ⇓ θ
─────────────────────
Γ ⊢ Solve(C) ⇓ θ

(Solve-Fail)
Γ ⊢ Unify(C) ⇑ c
───────────────────────
Γ ⊢ Solve(C) ⇑ c
```

Feature chapters call `Solve` once they have gathered the constraints for a typing context; the resulting `θ` is applied to recover concrete types from the synthesized type variables.

#### 7.3.4 Bidirectional synthesis and checking

The shared bidirectional skeleton consists of synthesis (`⇒`, "I can compute this expression's type bottom-up") and checking (`⇐`, "verify this expression against an expected type"). The framework rules owned by this chapter:

```text
(Syn-Expr)
Γ; R; L ⊢ e : T
────────────────────────────
Γ; R; L ⊢ e ⇒ T ⊣ ∅

(Syn-Ident)
(x : T) ∈ Γ
────────────────────────────────────
Γ; R; L ⊢ Identifier(x) ⇒ T ⊣ ∅

(Syn-Unit)
──────────────────────────────────────────────
Γ; R; L ⊢ TupleExpr([]) ⇒ TypePrim("()") ⊣ ∅

(Syn-Tuple)
n ≥ 1    ∀ i, Γ; R; L ⊢ e_i ⇒ T_i ⊣ C_i
──────────────────────────────────────────────────────────────────
Γ; R; L ⊢ TupleExpr([e_1, …, e_n]) ⇒ TypeTuple([T_1, …, T_n]) ⊣ ⋃_i C_i

(Syn-Call)
Γ; R; L ⊢ callee ⇒ TypeFunc(params, R_c) ⊣ C_0    Γ; R; L ⊢ ArgsOk_T(params, args)
──────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Call(callee, args) ⇒ R_c ⊣ C_0

(Syn-Call-Err)
Γ; R; L ⊢ Call(callee, args) ⇑ c
──────────────────────────────────────────────────
Γ; R; L ⊢ Call(callee, args) ⇒ T ⊣ C ⇑ c
```

`Syn-Expr` bridges the simple "has type" judgment `e : T` into the constraint-carrying synthesis judgment (yielding `∅` constraints). `Syn-Tuple` shows the canonical pattern: synthesize each operand, **union** the operand constraint sets, and assemble the result type. `Syn-Call` synthesizes the callee to a function type and checks the arguments against the parameter list via `ArgsOk_T` (owned by the calling chapter).

> *Feature-local ownership.* Synthesis and checking rules for records, enums, modal transitions, closures, arithmetic, indexing, and the rest are owned by their feature chapters. This chapter owns the shared unification, substitution, and judgment framework those rules consume — nothing more.

#### 7.3.5 Inference for literals

Literals are the primary source of *defaulting* in inference: an unannotated literal synthesizes a default type, while a literal checked against an expected type adopts that type. This is where annotation choices most directly change which `TypePrim` you end up with. The literal "has type" rules (`Γ ⊢ Literal(lit) : T`) lift into constraint-free synthesis (`⇒`) via `Syn-Literal`.

The suffix sets are fixed by the lexer: `IntSuffixSet = {i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize}` and `FloatSuffixSet = {f, f16, f32, f64}`. `IntSuffix(lit)` / `FloatSuffix(lit)` recover the suffix (or `⊥` when absent), and `NoFloatSuffix(lit)` holds when a float literal carries no suffix.

**Synthesis (no expected type).** An integer literal with an explicit suffix takes that suffix's type (when in range); an unsuffixed integer literal defaults to `i32`:

```text
(T-Int-Literal-Suffix)
lit.kind = IntLiteral    IntSuffix(lit) = t    InRange(IntValue(lit), t)
──────────────────────────────────────────────────────────────────────────
Γ ⊢ Literal(lit) : TypePrim(t)

(T-Int-Literal-Default)
lit.kind = IntLiteral    IntSuffix(lit) = ⊥    InRange(IntValue(lit), i32)
──────────────────────────────────────────────────────────────────────────
Γ ⊢ Literal(lit) : TypePrim(i32)
```

A float literal with an explicit width suffix (`f16`/`f32`/`f64`) takes that width; an unsuffixed float, or one with the width-inferring `f` suffix, defaults to `f32`:

```text
(T-Float-Literal-Explicit)
lit.kind = FloatLiteral    FloatSuffix(lit) = t    t ∈ {f16, f32, f64}
────────────────────────────────────────────────────────────────────────
Γ ⊢ Literal(lit) : TypePrim(t)

(T-Float-Literal-Infer)
lit.kind = FloatLiteral    (FloatSuffix(lit) = f ∨ NoFloatSuffix(lit))
────────────────────────────────────────────────────────────────────────
Γ ⊢ Literal(lit) : TypePrim(f32)
```

The remaining literal synthesis rules are fixed: `bool` literal → `TypePrim(bool)` (`T-Bool-Literal`), `char` literal → `TypePrim(char)` (`T-Char-Literal`), and a string literal → `TypeString(@View)` (`T-String-Literal`).

**Checking (an expected type is present).** When a literal is checked against an expected type, that type *governs* the literal — this is how a bare `5` becomes a `u64` or a bare `1.0` becomes an `f64`:

```text
(Chk-Int-Literal)
lit.kind = IntLiteral    T = TypePrim(t)    t ∈ IntTypes    InRange(IntValue(lit), t)
───────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Literal(lit) ⇐ T ⊣ ∅

(Chk-Float-Literal-Explicit)
lit.kind = FloatLiteral    FloatSuffix(lit) = s    s ∈ {f16, f32, f64}    T = TypePrim(s)
───────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Literal(lit) ⇐ T ⊣ ∅

(Chk-Float-Literal-Infer)
lit.kind = FloatLiteral    (FloatSuffix(lit) = f ∨ NoFloatSuffix(lit))    T = TypePrim(t)    t ∈ FloatTypes
──────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ Literal(lit) ⇐ T ⊣ ∅
```

Two consequences are load-bearing for developers:

- An **unsuffixed or `f`-suffixed float adopts the expected float type** in any width. But an **explicitly width-suffixed float must match the expected type exactly** — `Chk-Float-Literal-Explicit` requires `T = TypePrim(s)`. Writing `1.0f64` where an `f32` is expected is the error `E-TYP-1531` (§7.5).
- An integer literal checked against an integer type must be **in range** for that type; otherwise the check rule does not apply and the program is ill-typed.

There is a separate check rule for the *literal* `null` keyword against a **raw** pointer type: `Chk-Null-Literal` admits the `null` literal where `T = TypeRawPtr(q, U)`. The *safe* pointer null expression `Ptr::null()` is governed by the distinct rules in §7.3.6 — do not conflate the raw-pointer `null` literal with the safe-pointer null expression.

```ultraviolet
// Defaulting via synthesis: unannotated literals take their default types.
let tick = 0          // i32   (T-Int-Literal-Default)
let ratio = 0.5       // f32   (T-Float-Literal-Infer)

// Checking against an expected type re-types the same literals.
let frame_index: u64 = 0       // Chk-Int-Literal: 0 typed as u64
let gain: f64 = 0.5            // Chk-Float-Literal-Infer: 0.5 typed as f64

// Explicit suffix is authoritative on both paths.
let lane_mask = 0xFFu32        // T-Int-Literal-Suffix
let epsilon = 1.0e-6f64        // T-Float-Literal-Explicit
```

#### 7.3.6 Subsumption, pointer-null, and modal subsumption at use sites

The bridge from synthesis to checking is the **subsumption** rule: synthesize `S`, then demand `S <: T`. This is the single most important inference rule — it is where every subtyping coercion in §7.2 is actually applied.

```text
(Chk-Subsumption)
Γ; R; L ⊢ e ⇒ S ⊣ C    Γ ⊢ S <: T
────────────────────────────────────
Γ; R; L ⊢ e ⇐ T ⊣ C
```

Modal subsumption has a guarded failure: if the synthesized type is a state-specific modal type and the expected type is the general modal type but the state is **not** niche-compatible, the checker raises a dedicated diagnostic instead of silently failing subsumption (the value must be `widen`ed explicitly):

```text
(Chk-Subsumption-Modal-NonNiche)
Γ; R; L ⊢ e ⇒ S ⊣ C    StripPerm(S) = TypeModalState(modal_ref, S_s)
StripPerm(T) = ModalRefType(modal_ref)    ModalDeclOf(modal_ref) = M
¬ NicheCompatible(modal_ref, S_s)    c = Code(Chk-Subsumption-Modal-NonNiche)
──────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ e ⇐ T ⇑ c
```

The safe-pointer null expression is **check-only**: it has no synthesizable type and may only be used where a pointer that admits the null state is expected:

```text
PtrNullExpected(T) ⇔ T = TypePtr(U, s) ∧ s ∈ {Null, ⊥}

(Chk-Null-Ptr)
T = TypePtr(U, s)    s ∈ {Null, ⊥}
────────────────────────────────────
Γ; R; L ⊢ PtrNullExpr ⇐ T ⊣ ∅

(Syn-PtrNull-Err)
c = Code(PtrNull-Infer-Err)
──────────────────────────────────────────
Γ; R; L ⊢ PtrNullExpr ⇒ T ⊣ C ⇑ c

(Chk-PtrNull-Err)
¬ PtrNullExpected(T)    c = Code(PtrNull-Infer-Err)
────────────────────────────────────────────────────
Γ; R; L ⊢ PtrNullExpr ⇐ T ⊣ C ⇑ c
```

`PtrNullExpr` is the AST node produced by the surface expression `Ptr::null()`. In other words: a bare `Ptr::null()` cannot be inferred — there is nothing to infer the pointee from — and must always appear in a position with an explicit, null-admitting safe pointer type. Synthesis is always an error (`Syn-PtrNull-Err`); checking against a non-null-admitting type is an error (`Chk-PtrNull-Err`). Both surface as `E-TYP-1530`.

#### 7.3.7 Where types may be omitted, and where they are required

The grammar makes the type annotation optional on a binding:

```ebnf
binding_stmt ::= ("let" | "var") pattern (":" type)? binding_op expression terminator
binding_op   ::= "=" | ":="
```

(and identically for top-level static items: `binding_decl ::= pattern (":" type)? binding_op expression`).

A binding therefore comes in two typing modes — **annotated** and **inferred** — and the choice determines which rule applies.

**Annotated `let` (and `var`).** With an explicit annotation `T_a`, the initializer is *checked* against `T_a`, the pattern is checked against `T_a`, and the names are introduced at `T_a`:

```text
(T-LetStmt-Ann)
ty_opt = T_a    Γ; R; L ⊢ init ⇐ T_a ⊣ ∅    Γ ⊢ pat ⇐ T_a ⊣ B
Distinct(PatNames(pat))    IntroAll(Γ, B) ⇓ Γ'
──────────────────────────────────────────────────────────────
Γ; R; L ⊢ LetStmt(binding) ⇒ Γ' ▷ ⟨[], [], false⟩
```

Because the initializer is *checked*, annotation drives literal defaulting and subsumption: `let frame_count: u64 = 0` checks `0` against `u64`. If the initializer synthesizes a type that is not a subtype of the annotation, the binding is ill-typed:

```text
(T-LetStmt-Ann-Mismatch)
ty_opt = T_a    Γ; R; L ⊢ init ⇒ T_i ⊣ C    ¬(Γ ⊢ T_i <: T_a)    c = Code(T-LetStmt-Ann-Mismatch)
─────────────────────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ LetStmt(binding) ⇑ c
```

(This mismatch shares the assignment-mismatch error code `E-SEM-3133`.)

**Inferred `let` (and `var`).** With no annotation, the initializer is *synthesized* to `T_i`, the accumulated constraints are *solved* to a substitution `θ`, and the binding type is `θ(T_i)`:

```text
(T-LetStmt-Infer)
ty_opt = ⊥    Γ; R; L ⊢ init ⇒ T_i ⊣ C    Solve(C) ⇓ θ    T_b = θ(T_i)
Γ ⊢ pat ⇐ T_b ⊣ B    Distinct(PatNames(pat))    IntroAll(Γ, B) ⇓ Γ'
─────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ LetStmt(binding) ⇒ Γ' ▷ ⟨[], [], false⟩
```

If the constraints cannot be solved, inference fails:

```text
(T-LetStmt-Infer-Err)
ty_opt = ⊥    Γ; R; L ⊢ init ⇒ T_i ⊣ C    Solve(C) ⇑    c = Code(T-LetStmt-Infer-Err)
──────────────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ LetStmt(binding) ⇑ c
```

`var` bindings type identically: `T-VarStmt-Ann`, `T-VarStmt-Ann-Mismatch`, `T-VarStmt-Infer`, and `T-VarStmt-Infer-Err` are the same rules with `IntroAllVar` (introducing the names as mutable, `var`) in place of `IntroAll` (which introduces them as `let`). The two binding operators `=` and `:=` do not change the *type* assigned — `:=` marks the binding **immovable** (it affects move/ownership analysis in the statement/permissions chapters), not inference.

A `let`/`var` binding pattern must be **irrefutable**; a refutable pattern in binding position is rejected (`E-SEM-2711`) before typing concludes:

```text
(Let-Refutable-Pattern-Err)
pat ∈ {LiteralPattern(_), EnumPattern(_, _, _), ModalPattern(_, _), RangePattern(_, _, _)}
c = Code(Let-Refutable-Pattern-Err)
────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ pat ⇐ T ⇑ c
```

**Annotation is *required* (cannot be omitted) when:**

- the initializer is the safe-pointer null expression `Ptr::null()` (it is check-only; `Syn-PtrNull-Err` makes synthesis impossible), or any other check-only construct whose synthesis raises an inference error;
- the synthesized constraints leave a type variable unsolved (e.g. an empty container or a generic call whose result type variable is unconstrained), which makes `Solve` fail with `T-LetStmt-Infer-Err`;
- you want a *non-default* literal type at the binding (an unannotated `let x = 0` is always `i32`; to get `u64` you annotate or suffix).

**Annotation may be *omitted* when** the initializer synthesizes a fully concrete type — a call to a non-generic procedure, a record construction, a literal you are happy to take at its default, an already-typed identifier, and so on.

```ultraviolet
procedure currentFrame() -> u64 {
    return 0
}

// Omittable: the initializer synthesizes a concrete type.
let frame = currentFrame()        // T-LetStmt-Infer ⇒ u64
let tick = 0                      // T-LetStmt-Infer ⇒ i32 (default)

// Required: null pointer is check-only, so an annotation is mandatory.
let head: Ptr<u8>@Null = Ptr::null()   // T-LetStmt-Ann + Chk-Null-Ptr

// Required to override the literal default.
let budget: u64 = 0               // without the annotation this would be i32
```

#### 7.3.8 Inference for generics

Generic *declarations* introduce type parameters; generic *use* (a call to a generic procedure, or a generic type applied to arguments) is where inference does its real work. At a generic call site, the parameter types contain type variables; the call's argument checking generates equality constraints (`ArgsOk_T` over the instantiated parameter list, owned by the Generics chapter), and `Solve` discharges them to recover the instantiation `θ`. The structural unifier rule that drives generic-argument inference is `Unify-Apply`:

```text
(Unify-Apply)
T = TypeApply(path, [T_1, …, T_n])    U = TypeApply(path, [U_1, …, U_n])
──────────────────────────────────────────────────────────────────────────────
⟨UnifyStep({(T, U)} ∪ C, θ)⟩ → ⟨UnifyStep({(T_1, U_1), …, (T_n, U_n)} ∪ C, θ)⟩
```

Two applied generics unify only with the **same `path` and the same arity** (`Unify-Apply-Fail` otherwise), unifying argument-by-argument. Crucially, generic-argument inference uses **unification (`≡`-flavored)**, not subtyping: an argument's type must *unify* with the corresponding parameter type variable's eventual solution, not merely subtype it. Subtyping enters generic types only afterward, through declared **variance** (`Sub-Generic`, §7.2.7), when the *resulting* applied type is checked against an expected applied type.

A generic parameter is written with optional bounds and defaults. The Appendix B grammar:

```ebnf
generic_params     ::= "<" generic_param_list ">"
generic_param_list ::= generic_param (";" generic_param)*
generic_param      ::= identifier bound_clause? default_clause?
bound_clause       ::= "<:" class_bound_list
default_clause     ::= "=" type
class_bound_list   ::= class_bound ("," class_bound)*
class_bound        ::= type_path generic_args?
generic_args       ::= "<" type_arg_list ">"
type_arg_list      ::= type ("," type)* ","?
```

Note that `generic_param_list` separates parameters with **`;`**, while `type_arg_list` (at use sites) separates arguments with **`,`**. Type arguments may be omitted at a generic procedure call site when omitted-argument inference (`GenericCallInference`) succeeds; otherwise they are supplied explicitly with `generic_args`.

```ultraviolet
// A generic procedure introduces a type parameter TValue.
public procedure identity<TValue>(seed: TValue) -> TValue {
    return seed
}

// TValue is inferred to be i32 by unifying the argument type with the
// parameter type variable (the constraint α = i32 solves to α ↦ i32).
let echoed = identity(7)

// Explicit type arguments suppress inference and fix TValue directly.
let typed = identity<u64>(0)
```

#### 7.3.9 Inference for tuples, arrays, and unions

Compound literals propagate inference through their components. A tuple synthesizes the tuple of its component synthesized types and unions their constraints (`Syn-Tuple`). An array literal synthesizes `TypeArray(T, N)` where the element type unifies across all segments and `N` is the segment count (owned by the data-types/array chapter; the array *length* participates in equivalence/unification via `ConstLen`). Unions, as covered in §7.3.2, are *not* a vehicle for solving a variable: a union containing an unsolved type variable simply fails to unify (`Unify-Union-Fail`). When you need a union-typed binding to be inferred, ensure every member is concrete, or annotate.

```ultraviolet
// Tuple inference: each component is synthesized; the binding is (i32, f32, bool).
let sample = (0, 1.5, true)

// Array element type and length are both inferred: [i32; 3].
let lanes = [1, 2, 3]
```

---

### 7.4 Metatheoretic Properties

This section states the guarantees the type system is *designed* to satisfy. They are not rules you invoke; they are the invariants that make well-typed Ultraviolet programs safe. The spec defines a small-step machine over which the soundness properties are stated, then asserts the properties (formal proofs deferred to supplementary materials). For the working developer, the value of this section is knowing *what the type checker promises* so you can lean on it instead of re-checking by hand.

#### 7.4.1 The step relation

Evaluation is modeled as a stack machine with a focus, a continuation stack, and a store:

```text
Frame = ⟨ctor, vs, es⟩
  where ctor is an expression constructor of arity |vs| + 1 + |es|,
        vs are already-evaluated operand values (left of the hole),
        es are pending operand expressions (right of the hole),
        and operand order is Children_LTR
ScopeFrame = the block, key, region, and frame configurations of ExecState

K = [Frame | ScopeFrame]            (continuation stack, innermost first)
Config = ⟨Focus, K, σ⟩    Focus ∈ Expr ∪ {Done(out)}    out ∈ {Val(v), Ctrl(κ)}
```

The core stepping rules drive evaluation left-to-right into operand positions (`Step-Focus-Down`), advance to the next operand once a value is produced (`Step-Focus-Next`), reduce a fully-evaluated constructor application (`Step-Redex`), and unwind control effects through value frames (`Step-Ctrl-Unwind`):

```text
(Step-Focus-Down)
e = ctor(e_1, …, e_n)    n ≥ 1    ¬ Redex(e)    e_1 not a value
──────────────────────────────────────────────────────────────────
⟨e, K, σ⟩ → ⟨e_1, ⟨ctor, [], [e_2, …, e_n]⟩ :: K, σ⟩

(Step-Focus-Next)
──────────────────────────────────────────────────────────────────────────
⟨Done(Val(v)), ⟨ctor, vs, [e] ++ es⟩ :: K, σ⟩ → ⟨e, ⟨ctor, vs ++ [v], es⟩ :: K, σ⟩

(Step-Redex)
Γ ⊢ EvalSigmaBase(ctor(v_1, …, v_n), σ) ⇓ (out, σ')
──────────────────────────────────────────────────────────────────
⟨Done(Val(v_n)), ⟨ctor, [v_1, …, v_{n-1}], []⟩ :: K, σ⟩ → ⟨Done(out), K, σ'⟩

(Step-Ctrl-Unwind)
F is a Frame (not a ScopeFrame)
──────────────────────────────────────────────────────────
⟨Done(Ctrl(κ)), F :: K, σ⟩ → ⟨Done(Ctrl(κ)), K, σ⟩
```

`Redex(e)` holds when every operand position is a value or `e` is a leaf form (literals, names). `EvalSigmaBase` denotes exactly the base-case `EvalSigma`/`ExecSigma` rules — those whose premises contain no recursive `EvalSigma` on subexpressions. Composite forms with scoped or short-circuit evaluation — blocks, `if`, loops, calls, `key`/`region`/`frame` statements, `defer`, and pattern dispatch — do **not** use `Step-Focus-Down`; they push their own scope configurations (calls push the callee body block via `BlockEnter`), and the structured-control `Step-Exec-*` rules of the execution chapters are imported unchanged as steps over `ScopeFrame`s. Control effects `Ctrl(κ)` propagate through value frames by `Step-Ctrl-Unwind` and are intercepted by the innermost scope frame that handles them (loop frames absorb `Break`/`Continue`; cleanup runs per the `Step-Exec-*-Exit-Ctrl` rules). The whole-program relation is

```text
⟨e, σ⟩ →* (out, σ') ⇔ ⟨e, [], σ⟩ →* ⟨Done(out), [], σ'⟩
```

#### 7.4.2 The guarantees

**Coherence** ties the big-step and small-step semantics together — they agree:

```text
(Coherence)   Γ ⊢ EvalSigma(e, σ) ⇓ (out, σ') ⇔ ⟨e, σ⟩ →* (out, σ')
```

**Soundness** is the pair of progress and preservation, stated over well-typed configurations (a configuration `⟨e, K, σ⟩` is well-typed for `T` when `e` is well-typed at some `T_e` and `K` is a well-typed continuation from `T_e` to `T`: each `Frame`'s hole type matches the corresponding operand type of its constructor, and `ScopeFrame`s type per their owning chapters):

```text
(Progress)
If Γ; R; L ⊢ C : T and C is not ⟨Done(out), [], σ⟩, then C → C' for some C',
or C is blocked on a host primitive or a key acquisition.

(Preservation)
If Γ; R; L ⊢ C : T and C → C', then Γ; R; L ⊢ C' : T.
```

Together: a well-typed program never gets *stuck* (it either takes a step, finishes, or is legitimately blocked on a host primitive or a lock), and **types are preserved across every step** — the type you inferred at compile time is the type you have at run time, forever.

The remaining metatheorems are the memory-, permission-, and concurrency-safety guarantees the type and permission systems jointly enforce:

```text
(No-Use-After-Free)        A binding in state Moved or PartiallyMoved(F) where f ∈ F
                           cannot be read or moved from.
(No-Double-Free)           Each responsible binding is dropped exactly once when it
                           goes out of scope.
(No-Dangling-Pointers)     A Ptr<T>@Valid always references valid storage; a pointer
                           with provenance π cannot escape to longer-lived storage.
(Exclusivity-Invariant)    A unique, Active binding has no other live path to the same
                           storage.
(Permission-Preservation)  Permissions are preserved as regimes; a use site MUST NOT
                           create a weaker alias or turn unique into shared/const.
(State-Determinism)        Every binding has exactly one state in
                           {Valid, Moved, PartiallyMoved(F)} at each program point.
(No-Resurrection)          A Moved binding cannot return to Valid except by reassigning
                           a var binding.
(Data-Race-Freedom)        Concurrent accesses to shared data are serialized through the
                           key system.
(Fork-Join-Guarantee)      All work spawned in a parallel block completes before exit.
(Key-Serialization)        Tasks holding keys to overlapping paths with incompatible
                           modes never execute concurrently.
(Async-Key-Safety)         Keys cannot be held across yield/wait unless release is used.
```

These are the *return on investment* for writing in the type system: the style-guide directive to "express correctness in the code, not in comments" rests on these guarantees. When you encode a protocol in a `modal` type, `State-Determinism` and `No-Resurrection` make the state machine real; when you mark a binding `unique`, `Exclusivity-Invariant` and `Permission-Preservation` make exclusivity real; when you parallelize, `Data-Race-Freedom`, `Fork-Join-Guarantee`, and `Key-Serialization` make the concurrency real. The cross-references in the **Permissions**, modal-type, and concurrency chapters define the mechanisms; this section states what they buy you.

---

### 7.5 Core Type Diagnostics

This section owns the core type-inference and alias-cycle diagnostics that are not specific to a single feature chapter.

| Code         | Severity | Detection    | Condition                                                                                                  |
| ------------ | -------- | ------------ | ---------------------------------------------------------------------------------------------------------- |
| `E-TYP-1506` | Error    | Compile-time | Type alias cycle detected (`TypeAlias-Recursive-Err`)                                                       |
| `E-TYP-1530` | Error    | Compile-time | Type inference failed; unable to determine type (`PtrNull-Infer-Err`, `T-LetStmt-Infer-Err`, `Unify-Fail`) |
| `E-TYP-1531` | Error    | Compile-time | Float literal explicit suffix does not match expected type                                                  |

**`E-TYP-1506` — type alias cycle.** A type alias whose expansion (transitively) refers back to itself is rejected. The alias graph is checked with `AliasCycle(p) ⇔ p ∈ Reach⁺(AliasGraph, p)`; when an alias `p` is reachable from itself, alias well-formedness raises `TypeAlias-Recursive-Err`:

```text
AliasCycle(p)    c = Code(TypeAlias-Recursive-Err)
────────────────────────────────────────────────────
Γ ⊢ p : TypeAliasOk ⇑ c
```

This is what keeps `AliasExpand` (used by `≡` via `TypeKeyString`) terminating.

**`E-TYP-1530` — inference failure.** This is the umbrella inference-failure code. It is raised by three distinct underlying failures:

- `PtrNull-Infer-Err` — a `Ptr::null()` used in a position that cannot supply a null-admitting safe pointer type (synthesis `Syn-PtrNull-Err`, or a check `Chk-PtrNull-Err` against a non-null-admitting type);
- `T-LetStmt-Infer-Err` — an unannotated `let`/`var` whose initializer constraints cannot be solved (`Solve(C) ⇑`);
- `Unify-Fail` — the unifier reached `UnifyFail` (constructor mismatch, arity mismatch, occurs-check failure, a union with an unsolved variable, a refinement predicate mismatch, and so on).

The remedy is almost always to **add a type annotation** that supplies the missing information, or to make the construct concrete (suffix a literal, give an empty container an element type, fix a generic's type arguments).

**`E-TYP-1531` — float-suffix mismatch.** Using an explicit width suffix on a float literal that conflicts with the expected type is an error. Per the literal rules, `Chk-Float-Literal-Explicit` requires `T = TypePrim(s)` where `s` is the suffix; if the expected float type differs from the suffix, no checking rule applies and this code is raised. Unsuffixed and `f`-suffixed floats, by contrast, *adopt* the expected float type and never trigger this.

```ultraviolet
// E-TYP-1531: explicit f64 suffix conflicts with the expected f32.
// let gain: f32 = 1.0f64

// Correct: drop the explicit width and let the expected type govern,
// or use the width-inferring `f` suffix.
let gain: f32 = 1.0          // Chk-Float-Literal-Infer adopts f32
let trim: f32 = 1.0f         // `f` suffix: width inferred from context
```

---

### 7.6 Idioms & Best Practices

These practices follow directly from the rules above and from the project style guide ("express correctness in the code… use the type system… before reaching for weaker runtime-only validation").

- **Annotate at API boundaries; infer inside.** Public and cross-module procedure signatures, fields, and exported constants must be fully and explicitly typed — annotations are part of the API contract, and the style guide requires explicit visibility and contracts at boundaries. Inside a procedure body, prefer inference (`T-LetStmt-Infer`) for local `let`/`var` bindings whose initializers synthesize a concrete type; it reduces ceremony without losing safety.

- **Annotate to override a literal default, not to restate an obvious one.** A bare `let x = 0` is always `i32` and `let r = 0.5` is always `f32`. When you need a different width, *either* annotate (`let n: u64 = 0`) *or* suffix (`let n = 0u64`) — but do not annotate a binding whose initializer already synthesizes exactly the type you want; that is redundant ceremony the style guide discourages.

- **Prefer the width-inferring `f` suffix (or no suffix) over a hard width suffix in checked positions.** `let gain: f32 = 1.0` and `let gain: f32 = 1.0f` both work and stay flexible; `1.0f64` in an `f32` slot is `E-TYP-1531`. Reserve explicit `f16`/`f32`/`f64` suffixes for synthesis positions where no expected type exists.

- **Reach for unions through `≡`/subtyping, not through inference.** Because the unifier refuses to solve a variable through a union (`Unify-Union-Fail`), give union-typed bindings explicit annotations. Lean on `Sub-Member-Union` and `Sub-Union-Width` (which are permutation-insensitive) to flow narrower unions into wider ones — but remember `Member` uses `≡`, so state discriminators (e.g. `string@View` vs a bare `string`) must match.

- **Use aliases freely; they are transparent.** `AliasExpand` makes a `type` alias equivalent to its body for `≡`. Aliases improve legibility (a style-guide value) without introducing a new nominal type. Just avoid cycles (`E-TYP-1506`).

- **`widen` non-niche modal states explicitly.** Only niche-compatible states subsume to the general modal type via `Sub-Modal-Niche`. For any other state, write `widen` rather than expecting subsumption — the checker will otherwise raise `Chk-Subsumption-Modal-NonNiche`. Model lifecycle with `modal` types (per the style guide) and let `State-Determinism`/`No-Resurrection` enforce the protocol.

- **Encode constraints with refinements, but spell predicates consistently.** Refinement equivalence is structural after subject renaming; `self > 0` and `0 < self` are *not* equivalent for `≡` or unification. Pick one canonical phrasing for a given constraint and reuse it, so refinement types compose without surprise mismatches. In a standalone refinement type the constrained value is always `self`; in a parameter refinement it is the parameter's own name (never `self`).

- **Let `!` do the work in diverging branches.** A `return`, an infinite `loop`, or a never-returning call has type `!`, which subsumes everywhere (`Sub-Never`). You never need to fabricate a placeholder value in an unreachable branch.

### 7.7 Pitfalls & Diagnostics

- **No implicit numeric widening — ever.** `i32` is not `<: i64`; `f32` is not `<: f64`. Assigning an `i32` value where an `i64` is expected does **not** compile (it is `T-LetStmt-Ann-Mismatch`/`E-SEM-3133` at a binding, or a checked-subsumption failure at an argument). Use an explicit `as` cast. This is the single most common surprise for newcomers.

- **`Ptr::null()` cannot be inferred.** A binding `let p = Ptr::null()` is `E-TYP-1530` (via `PtrNull-Infer-Err`): the safe-pointer null expression has no synthesizable type. Always annotate the pointee and a null-admitting state, e.g. `let p: Ptr<u8>@Null = Ptr::null()`. (The raw-pointer `null` literal is a different construct, governed by `Chk-Null-Literal` against `*imm T`/`*mut T`.)

- **Arrays and slices are invariant in their element type.** `Sub-Array`/`Sub-Slice` and `Unify-Array`/`Unify-Slice` all require element *equivalence*, not subtyping. A `[Cat; 3]` is not usable where `[Animal; 3]` is expected even if `Cat <: Animal`. Tuples and ranges, by contrast, are covariant.

- **Array length mismatches are hard errors, not coercions.** `[i32; 3]` and `[i32; 4]` neither unify (`Unify-Array-Len-Fail`) nor relate by subtyping. There is no truncation or padding. If a length is computed, make sure it is a *pure, capability-free* compile-time expression so `ConstLen-Comptime` can evaluate it; otherwise you get `ConstLen-Err`.

- **Unions do not solve type variables.** A constraint between a union containing an unsolved variable and anything fails (`Unify-Union-Fail`), surfacing as `E-TYP-1530`/`Unify-Fail`. If you see inference fail around a union, annotate the binding.

- **Refinement and modal-state equality are exact.** Distinct subject-normalized predicates fail to unify (`Unify-Refine-Pred-Fail`); distinct modal states or rigid heads (path, string, bytes, dynamic, opaque, full-range) fail directly (`Unify-Rigid-Fail`). The fix is to make the predicates / states actually match, not to expect the checker to prove them equal.

- **State discriminators are part of the type.** `TypeString(@View)`, `TypeString(@Managed)`, and a bare `string` (`TypeString(⊥)`) are pairwise inequivalent; likewise for `bytes` and for the safe pointer states `@Valid`/`@Null`/`@Expired`/`⊥`. A string literal is `@View`, so it is not a member of a `... | string` union whose member is a bare `string`.

- **Permission qualifiers must match for subtyping.** `Sub-Perm` requires `p = q`. A `unique T` does not subsume into a `shared T` or `const T` through type subtyping (and `Permission-Preservation` forbids weakening at use sites). Permission *admissibility* is a separate relation (`PermAdmits`) owned by the Permissions chapter.

- **An over-eager annotation can introduce a mismatch.** Because an annotated binding *checks* the initializer (`T-LetStmt-Ann`), annotating with a type the initializer's synthesized type does not subtype yields `T-LetStmt-Ann-Mismatch` (`E-SEM-3133`). When in doubt about the exact synthesized type, omit the annotation and let inference report the type, then annotate deliberately.

- **`=` versus `:=` does not change the type.** Both binding operators produce the same binding type; `:=` only makes the binding **immovable** (a moves/ownership property covered in the statement and permissions chapters). Do not reach for one over the other to influence inference — it has no effect on typing.
