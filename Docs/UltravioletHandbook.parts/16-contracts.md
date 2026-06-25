## 16. Contracts: Preconditions, Postconditions, Invariants & Verification

Contracts are how Ultraviolet states machine-checkable obligations directly in code. A **contract clause** records what a procedure assumes about its inputs (the **precondition**) and what it guarantees about its result and post-state (the **postcondition**). An **invariant** records a property that a type or a loop must always preserve. The compiler *proves* these obligations statically wherever it can; only inside an explicit `#dynamic` scope does it fall back to inserting a runtime check. The style guide is emphatic on this point: *if a rule about safety, range, state, ownership, lifetime, authority, or valid sequencing can be expressed with contracts or invariants, express it in code* — contracts are mandatory where expressible, and public, cross-module, lifecycle, and FFI surfaces should be especially strict.

This chapter covers the contract grammar (§15.4), preconditions (§15.5), postconditions and the `@result`/`@entry` intrinsics (§15.6), type and loop invariants (§15.7), the verification logic and the static-proof-versus-dynamic-check decision (§15.8), behavioral subtyping obligations for class implementations (§15.9), and the diagnostics that govern all of the above (§15.10). Foreign contracts on `extern` procedures (§23.6) are covered at the end because they reuse the same machinery with a distinct `@` decorator family. For procedure and method declaration mechanics see the Procedures, Methods & Overloading chapter (§15.1–15.2); for refinement types — which share the `|: { ... }` predicate form — see the Refinement Types chapter (§14.8 / B.2 `refinement_clause`); for the `is`/`as` narrowing forms used inside postconditions see the Control Expressions and Pattern Matching chapters.

> **Note on tokens.** Ultraviolet does **not** spell contract clauses with English keywords such as `requires` or `ensures`. The surface tokens are the operators `|:` (open a contract clause, an invariant, or a refinement), `|=` (separate precondition from postcondition), and the `@` decorator spellings `@result` / `@entry(...)` for postcondition-only contract intrinsics. Use exactly these source spellings. They are token sequences, not combined lexer tokens. The style guide informally writes the dynamic-verification opt-in as `[[dynamic]]`, but the **normative surface syntax is the attribute `#dynamic`** (Appendix B.8 `dynamic_attribute ::= "#" "dynamic"`); the spec is authoritative for syntax, so this chapter uses `#dynamic` throughout.

### 16.1 The Contract Clause (§15.4)

#### 16.1.1 Exact syntax

A contract clause attaches to a procedure or method **after the signature and before the body block**. The grammar, reproduced verbatim from §15.4.1 and Appendix B.7:

```ebnf
contract_clause    ::= "|:" contract_body
contract_body      ::= precondition_expr "|=" postcondition_expr
                     | "|=" postcondition_expr
                     | precondition_expr
precondition_expr  ::= predicate_expr
postcondition_expr ::= predicate_expr
predicate_expr     ::= logical_or_expr
```

The placement is fixed by the declaration grammar. For a free procedure (Appendix B.6):

```ebnf
procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
signature      ::= "(" param_list? ")" ("->" return_type)?
```

and for a method (§15.2.1 / Appendix B.6 `method_def`):

```ebnf
method_def         ::= attribute_list? visibility? "override"? "procedure" identifier generic_params? "(" receiver ("," param_list)? ")" ("->" type)? contract_clause? block_expr
receiver           ::= receiver_shorthand | explicit_receiver
receiver_shorthand ::= "~" | "~!" | "~%"
explicit_receiver  ::= param_mode? "self" ":" type
```

So the canonical order for a free procedure is `procedure name(...) -> RetType |: PRE |= POST { body }`, and for a method `procedure name(~recv, ...) -> RetType |: PRE |= POST { body }`. In both forms, the contract clause follows the optional return type directly. A contract clause is **optional**; a procedure with no `|:` simply has precondition `true` and postcondition `true`.

The three `contract_body` alternatives give three shapes:

| Written form | Precondition | Postcondition |
| ------------ | ------------ | ------------- |
| `\|: P`       | `P`    | `true` |
| `\|: P \|= Q`  | `P`    | `Q`    |
| `\|: \|= Q`     | `true` | `Q`    |
| (no clause)  | `true` | `true` |

These elision rules are normative — they are the precondition table of §15.5.4 and the postcondition table of §15.6.4. The parser distinguishes the post-only form by seeing `|=` immediately after `|:` (rule **Parse-ContractBody-PostOnly**), the pre+post form by parsing a predicate and then seeing `|=` (rule **Parse-ContractBody-PrePost**), and the pre-only form when no `|=` follows (rule **Parse-ContractBody-PreOnly**).

A clause that opens with `|:` immediately followed by `@foreign_assumes` or `@foreign_ensures` is **not** an ordinary contract clause; it is a foreign contract clause (the `ForeignContractStart` predicate of §23.6.2), parsed by §23.6 and discussed in §16.8. Likewise, `|: {` (with a brace) is an **invariant** or a **refinement**, not a contract clause — the parser keys on whether `|:` is immediately followed by `{` (rule **Parse-InvariantOpt-Yes**); see §16.4.

> **Line-continuation note.** Ultraviolet's lexer treats a newline as a *continuation* (not a statement boundary) when the preceding physical line ends in a non-unary binary operator, or while inside an open bracket pair (the `Continue` rule of §4.1.7). Because the procedure header — signature, contract clause, and opening `{` — is a single syntactic unit with no terminators between its parts, a multi-line contract MUST keep every interior newline continued. The reliable formatting is to place the operators `|:` and `|=` at the **end** of the line they open, and to put the body's opening `{` on the same physical line as the final predicate token (as the examples below do). A short contract may simply share the signature's line. A newline placed *before* `|:`, `|=`, or `{` would terminate the header and detach the contract, so avoid it.
>
> One further subtlety: a postcondition predicate is parsed with the full expression grammar (`ParsePredicateExpr ⇔ ParseExpr`, §15.4.2), so if the predicate would otherwise **end in a bare identifier** placed immediately before the body `{`, wrap the whole postcondition in parentheses (`|= ( ... ) {`). This prevents the parser from reading `identifier {` as a record literal that swallows the body. A predicate ending in a literal, a field access, an index, or `)` needs no such guard.

#### 16.1.2 Semantics: predicates must be `bool` and pure

Both halves of a contract are predicate expressions, and both are subject to two static well-formedness obligations (rule **WF-Contract**, §15.4.4):

1. **The predicate must have type `bool`.** A non-`bool` predicate is rejected with `E-SEM-2808`.
2. **The predicate must be pure.** An impure expression in a contract predicate is rejected with `E-SEM-2802`; an impure expression in a contract clause is `E-SEM-3004`.

Purity here is defined precisely by the `pure` judgment of §15.4.4. The pure forms include: literals; identifiers in scope; field, tuple, and index access on pure subexpressions; the unary operators `!`, `-`, `*`; the binary operators in

```text
PureOps = { +, -, *, /, %, **, ==, !=, <, <=, >, >=, &&, ||, &, |, ^, <<, >>, .., ..= }
```

casts (`as`); `if` / `if ... is` / `if ... is { ... }` expressions whose parts are pure; pure blocks, tuples, arrays, and record literals; calls to builtins marked pure (`sizeof`, `alignof`, `type_name`, …); calls to procedures that take **no capability parameters** and are pure procedures; `const`-receiver method calls that are pure and capability-free; and pure `comptime` calls whose argument and result types are valid for compile-time evaluation.

The following are **never** pure and therefore may not appear in a contract predicate (§15.4.4): assignment expressions, mutable method calls, allocation expressions (`new` and `~>alloc`), spawn/dispatch/parallel expressions, yield/wait expressions, procedure calls with capability parameters, and `unsafe` blocks. A contract predicate is a *static verification context*; pure compile-time procedures may be evaluated there, and such calls are absent from runtime item lowering.

The two halves see different evaluation contexts:

- **Precondition context `Γ_pre`** includes the receiver (if any) and all procedure parameters **at entry state**. It excludes `@result`, `@entry`, module-scope bindings, enclosing locals, and body-local bindings.
- **Postcondition context `Γ_post`** includes the receiver, all parameters, `@result`, and `@entry`. On the right of `|=`, a mutable parameter or mutable receiver denotes its **post-state** value, while `@entry(...)` denotes its **entry-state** value.

A contract clause has **no independent runtime effect** (§15.4.5). Its operational impact is solely through verification and through the `ContractCheck` forms inserted by §15.8 when (and only when) static proof fails inside a `#dynamic` scope. No lowering is defined directly for the clause (§15.4.6).

#### 16.1.3 Worked example: a contract on a public API

```ultraviolet
/// Returns `numerator` divided by `denominator`, truncated toward zero.
///
/// Precondition: the numerator is non-negative and the denominator is positive.
/// Postcondition: the quotient is non-negative and multiplying it back never
/// exceeds the numerator.
public procedure dividePositive(numerator: i64, denominator: i64) -> i64 |:
    numerator >= 0 && denominator > 0 |=
    (@result >= 0 && @result * denominator <= numerator) {
    return numerator / denominator;
}
```

The precondition `numerator >= 0 && denominator > 0` is the caller's obligation; it pins the domain so that truncation toward zero coincides with the floor and the postcondition is genuinely true. The postcondition refers to the returned value through `@result`. Both predicates are `bool`-typed and pure, so the clause is well-formed.

### 16.2 Preconditions (§15.5)

#### 16.2.1 Syntax and elision

The precondition is the predicate to the **left** of `|=`, or the entire contract body when `|=` is absent (§15.5.1). Its parsing is part of `ParseContractBody` (§15.4.2). The elided value is computed by `PreconditionOf` (§15.5.3):

```text
PreconditionOf(⊥)            = true        (no contract clause)
PreconditionOf(⟨pre, post⟩)  = true        if pre = ⊤
PreconditionOf(⟨pre, post⟩)  = pre         otherwise
```

#### 16.2.2 Semantics: the caller proves the precondition

A precondition is an obligation discharged **at the call site**, not inside the callee. Rule **Pre-Satisfied** (§15.5.4) makes this explicit: a call `f(a_1, …, a_n)` is valid at call-site program point `S_call` exactly when the callee's precondition `P_pre`, after substituting the actual arguments, is statically proven in the proof context active at `S_call`:

```text
Γ ⊢ f : (T_1, …, T_n) → R    precondition(f) = P_pre    StaticProofAt(S_call, Γ_S, P_pre)
─────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ f(a_1, …, a_n) @ S : valid
```

"The caller is responsible for satisfying the precondition." Diagnostics for an unsatisfied precondition attach to the **call site** (§15.5.7), not the declaration.

When runtime verification is selected (the call appears in a `#dynamic` scope and static proof fails), the precondition is evaluated **before** the procedure body executes and **before** any `@entry` capture (§15.5.5). The precondition evaluates in the empty contract environment `ρ_∅` (§15.8.5); it sees only parameters and the receiver at entry.

#### 16.2.3 Worked example: a precondition the caller can prove

```ultraviolet
/// Reads the element at `index`.
///
/// Precondition: `index` is within bounds.
public procedure elementAt(values: [i32; 8], index: usize) -> i32 |: index < 8 {
    return values[index];
}

/// Sums the first three elements; every index is a literal the compiler
/// proves `< 8`, so the precondition is discharged statically.
public procedure sumFirstThree(values: [i32; 8]) -> i32 {
    let a: i32 = elementAt(values, 0);
    let b: i32 = elementAt(values, 1);
    let c: i32 = elementAt(values, 2);
    return a + b + c;
}
```

The compiler proves `0 < 8`, `1 < 8`, and `2 < 8` by constant propagation and linear-integer reasoning (mandatory proof techniques 1 and 2 of §15.8.4). No precondition check is emitted because the obligation is discharged at compile time.

### 16.3 Postconditions: `@result` and `@entry` (§15.6)

#### 16.3.1 Syntax

The postcondition is the predicate to the **right** of `|=`. Two intrinsics are available **only** inside postconditions (§15.6.1, Appendix B.7):

```ebnf
postcondition_expr ::= predicate_expr
contract_intrinsic ::= decorated_identifier("@", "result")
                     | decorated_identifier("@", "entry") "(" expression ")"

(* The @ spellings are decorated identifiers: Operator("@") followed by the
   named identifier. Contract intrinsics parse in any primary-expression
   position; `@result` outside a postcondition and `@entry` outside a contract
   predicate are rejected statically (E-SEM-2806, E-CON-0415, E-CON-0416). *)
```

The elided postcondition is computed by `PostconditionOf` (§15.6.3):

```text
PostconditionOf(⊥)            = true     (no contract clause)
PostconditionOf(⟨pre, post⟩)  = true     if post = ⊥
PostconditionOf(⟨pre, post⟩)  = post     otherwise
```

#### 16.3.2 `@result` — the returned value

`@result` denotes the value the procedure returns, governed by these rules (§15.6.4):

1. It is available **only** in postcondition expressions. Used anywhere else, it is `E-SEM-2806`.
2. Its type is the **declared return type** of the enclosing procedure.
3. For a unit-returning procedure, `@result` has type `()`.

Postcondition verification happens at each return point `r` after `@result` is bound to the returned value (rule **Post-Valid**, §15.6.4):

```text
postcondition(f) = P_post    ∀ r ∈ ReturnPoints(f). Γ_r ⊢ P_post : satisfied
──────────────────────────────────────────────────────────────────────────
f : postcondition-valid
```

`@result` interacts with the type system at full strength:

- When the return type is a union `T_1 | T_2 | … | T_n`, `@result` has that union type (**Result-Union-Type**).
- `@result is T_variant` is a `bool` test for a variant `T_variant ∈ Variants(T_union)` (**Result-Is-Predicate**).
- A successful test narrows `@result as T_variant` to that variant (**Result-Narrowing**).
- When the return type is a modal type `M@S`, `@result : M@S` (**Result-Modal**); when it is a type parameter `T`, `@result : T` (**Result-Generic**).
- Using a constrained operator on a generic `@result` imposes the corresponding class bound on the return type parameter in the signature (**Result-Generic-Constraint**).
- Error-propagation returns are covered: for a `T_success | T_error` return, when `e?` propagates an error at point `p`, the postcondition is evaluated for the propagation return with `@result` bound to the propagated error value (**Propagate-Postcondition**).

```ultraviolet
/// Looks up a record by id.
///
/// Postcondition: on the `Found` branch the returned id matches the request;
/// otherwise the `Missing` branch records what was requested.
public record Found {
    id: u32
}

public record Missing {
    requested: u32
}

public procedure lookup(id: u32) -> Found | Missing |: |=
    (@result is Missing) ||
    ((@result is Found) && ((@result as Found).id == id)) {
    if id == 0 {
        return Missing { requested: id };
    }
    return Found { id: id };
}
```

Here the post-only contract form `|: |= Q` is used. The predicate tests the union variant with `is` and narrows with `as` to read the field — both forms are exactly the **Result-Is-Predicate** and **Result-Narrowing** rules above (these `is`/`as` forms on `@result` are sanctioned specifically for postconditions by §15.6.4). The `(@result is Missing)` disjunct keeps the postcondition true on the `Missing` return. The trailing `||` makes the predicate a single logical line that continues onto the next physical line.

#### 16.3.3 `@entry(expr)` — entry-state snapshots

`@entry(expr)` captures the value of `expr` as evaluated **at entry**, so a postcondition can compare the post-state against the pre-state. It is constrained as follows (§15.6.4):

1. It is available **only** in postcondition expressions.
2. `expr` **must be pure**.
3. `expr` **must reference only parameters and the receiver**.
4. The result type of `expr` **must satisfy `BitcopyType`** (rule **Entry-Type**: from `Γ_post ⊢ e : T` with `BitcopyType(T)` derive `Γ_post ⊢ @entry(e) : T`).

The capture happens by **bitwise copy**, at a precise point in the call timeline (§15.6.5):

```text
1. Parameter Binding
2. Precondition Check
3. @entry Capture
4. Body Execution
5. Postcondition Check
6. Return
```

All `@entry` expressions in a procedure's postcondition are collected and evaluated once, immediately after parameter binding and a successful precondition check (rule **EntryCapturePhase**); every postcondition check for the invocation then reuses the captured values. Because capture is `Capture(v, T) = v` only when `BitcopyType(T)`, `@entry` may only snapshot trivially-copyable data — `@entry` on a non-`Bitcopy` type is `E-SEM-2805`; a side-effecting `@entry` expression is `E-CON-0416`; one requiring a capability is `E-CON-0415`; and `@entry` over a parameter whose value is no longer available after binding (e.g. moved away) is `E-SEM-2807`.

```ultraviolet
/// A monotonic counter bounded by `maximum`.
public record Counter {
    value: u64
    maximum: u64

    /// Increments the counter and reports the new value.
    ///
    /// Precondition: there is headroom below the maximum.
    /// Postcondition: the post-state value is exactly one greater than at entry,
    /// and equals the returned value.
    public procedure tick(~!) -> u64 |:
        self.value < self.maximum |=
        self.value == @entry(self.value) + 1 && @result == self.value {
        self.value = self.value + 1;
        return self.value;
    }
}
```

The receiver shorthand `~!` is a **unique** (mutable) receiver, so within the body `self` denotes the mutable receiver. On the left of `|=`, `self.value` in the precondition is the entry value; on the right, `self.value` denotes the **post-state** value, while `@entry(self.value)` recalls the **entry** value. The fields read are `u64`, which is `Bitcopy`, so the `@entry` capture is well-formed.

### 16.4 Invariants (§15.7)

Invariants come in two kinds — **type invariants** attached to a type declaration, and **loop invariants** attached to a `loop`. Both use a braced predicate, which is what distinguishes them syntactically from an ordinary contract clause.

#### 16.4.1 Exact syntax

```ebnf
type_invariant ::= "|:" "{" predicate_expr "}"
loop_invariant ::= "|:" "{" predicate_expr "}"
```

A type invariant attaches **after** the closing brace of a `record`, `enum`, or `modal` declaration body (Appendix B.6):

```ebnf
record_decl ::= attribute_list? visibility? "record" identifier generic_params? implements_clause? "{" record_body "}" type_invariant?
enum_decl   ::= attribute_list? visibility? "enum"   identifier generic_params? implements_clause? "{" variant_members? "}" type_invariant?
modal_decl  ::= attribute_list? visibility? "modal"  identifier generic_params? implements_clause? "{" state_block+ "}" type_invariant?
```

A loop invariant attaches between the loop condition and the loop body (Appendix B.3):

```ebnf
loop_expr      ::= "loop" loop_condition? loop_invariant? block_expr
loop_condition ::= expression | pattern (":" type)? "in" expression
```

The parser recognizes an invariant only when `|:` is **immediately followed by `{`** (rules **Parse-InvariantOpt-Yes / -None**); a `|:` followed by anything else is a contract clause or, in type position, a refinement.

#### 16.4.2 Type-invariant semantics

Inside a type invariant (§15.7.4):

1. `self` denotes an instance of the type being defined.
2. Field access on `self` is permitted.
3. Method calls on `self` are permitted **only when the method is pure**.

Type invariants are enforced at three categories of program point:

1. **Post-construction** — after a value of the type is constructed.
2. **Before any public receiver-taking procedure call** — the instance must be valid on entry to public methods.
3. **Before any mutating receiver-taking procedure returns** — a mutator must re-establish the invariant before returning.

Two structural rules follow. First, **a type with an invariant MUST NOT declare public mutable fields** — otherwise external code could break the invariant without passing through a checked entry point; violating this is `E-SEM-2824`. Second, **private procedures are exempt from the pre-call enforcement point**; the invariant is rechecked when control returns to a public caller (so a private helper may transiently break and restore the invariant).

The corresponding static violations are diagnosed individually: at construction `E-SEM-2820`; at public entry `E-SEM-2821`; at mutator return `E-SEM-2822`; at a private-to-public return `E-SEM-2823`. Under runtime verification the same points are checked dynamically (§15.7.5).

```ultraviolet
/// A clamped percentage that is always in the inclusive range 0..=100.
public record Percentage {
    value: u8

    /// Constructs a percentage, clamping the input into range.
    public procedure clamp(raw: u8) -> Self {
        if raw > 100 {
            return Self { value: 100 };
        }
        return Self { value: raw };
    }

    /// Returns the stored percentage.
    public procedure get(~) -> u8 {
        return self.value;
    }
} |: { self.value <= 100 }
```

The braced invariant `|: { self.value <= 100 }` trails the closing brace of the record body. The single field `value` is **not** declared `public`, so it is private by default, which satisfies the no-public-mutable-field rule; the only way to obtain a `Percentage` is through the `clamp` factory, whose result is checked post-construction. The read accessor `get(~)` uses the `~` (const) receiver shorthand. This record needs no `implements_clause`; any generic constraints would be written as bounds in its `generic_params` list.

#### 16.4.3 Loop-invariant semantics

A loop invariant is enforced at three points (§15.7.4):

1. **Before the first iteration** (initialization). Failure is `E-SEM-2830`.
2. **At the start of every subsequent iteration** (maintenance). Failure is `E-SEM-2831`.
3. **Immediately after loop termination.**

On successful termination verification the implementation generates a **verification fact** for the invariant at loop exit (so code after the loop may rely on it). Under runtime verification, the loop invariant is checked at loop entry and at every back-edge, **including `continue` paths** (§15.7.5).

```ultraviolet
/// Sums `0 + 1 + ... + (n-1)` using an accumulator loop.
///
/// The loop invariant ties the loop counter to the bound, giving the verifier
/// a fact to reason with at and after the loop.
public procedure triangular(n: u32) -> u64 {
    var acc: u64 = 0;
    var i: u32 = 0;
    loop i < n |: { i <= n } {
        acc = acc + (i as u64);
        i = i + 1;
    }
    return acc;
}
```

The invariant `|: { i <= n }` sits between the loop condition `i < n` and the body. It holds before the first iteration (`0 <= n`), is maintained across each iteration (from `i < n` the body's `i = i + 1` yields `i <= n`), and is true at exit. The verifier emits the fact `i <= n` after the loop.

### 16.5 Verification Logic: what is proved vs checked (§15.8)

#### 16.5.1 The decision: static proof first, dynamic check only in `#dynamic`

Verification introduces **no surface syntax** (§15.8.1). It decides, per obligation, between three outcomes (§15.8.4):

```text
(Contract-Static-OK)     StaticProofAt(S, Γ_S, P)                          ⟹ P : verified
(Contract-Static-Fail)   ¬ StaticProofAt(...)  ∧  ¬ InDynamicContext        ⟹ program is ill-formed
(Contract-Dynamic-Check)  ¬ StaticProofAt(...)  ∧  InDynamicContext          ⟹ emit ContractCheck(P, k, s, ρ)
```

In words: **the compiler tries to prove every contract obligation at compile time. If it succeeds, nothing is emitted (the obligation is `verified`). If it fails and the obligation is not inside a `#dynamic` scope, the program is rejected** with `E-SEM-2801` ("contract predicate not provable outside `#dynamic` scope"). **Only inside a `#dynamic` scope does a failed proof become a runtime check** rather than a compile error. This is the central rule of the contract system: contracts are *proofs*, and dynamic checking is the explicitly-opted-in fallback.

`#dynamic` is an attribute applied to an enclosing declaration or expression; its scope is **lexical** and does **not** propagate through procedure calls. It may not be applied directly to a contract clause or predicate (`E-CON-0410`), a `type` alias declaration (`E-CON-0411`), or a field declaration (`E-CON-0412`). If a `#dynamic` scope ends up producing no checks at all because every proof succeeded statically, the implementation warns (`W-CON-0401`). The style guide reinforces this: do not use the dynamic mode to bypass correct static conformance or to compensate for poor API design, weak type modeling, or missing contracts.

#### 16.5.2 What the compiler may reason with

The mandatory proof techniques an implementation must apply (§15.8.4) are:

1. Constant propagation
2. Linear integer reasoning
3. Boolean algebra
4. Control flow analysis
5. Type-derived bounds
6. Verification facts

The proof context at a program point `S` is

```text
ProofContextAt(S) = FlowFactsAt(S) ∪ ContractFactsAt(S)
```

i.e. the facts that dominate `S` from control flow (`FlowFactsAt(S) = { P | F(P, L) ∈ Facts ∧ L dom S }`), plus the conjuncts of the enclosing procedure's precondition that remain in scope at `S` (`ContractFactsAt(S)`). A predicate is statically proven exactly when it is both **decidable** and **entailed** by that context:

```text
StaticProofAt(S, ProofContextAt(S), P) ⇔ Decidable(P) ∧ ProofContextAt(S) ⊢ P
```

`Decidable(P)` is the smallest set closed under: the literals `true` / `false`; comparisons of linear integer expressions over literals and variables; syntactic equality up to alpha-renaming between identifiers and literal constants; and boolean combinations using `!`, `&&`, `||`. Entailment is derived by the rules **Ent-True**, **Ent-Fact**, **Ent-And**, **Ent-Or-L / Ent-Or-R**, and **Ent-Linear**. Linear-integer entailment is defined over `LinPred` (comparisons of linear expressions `∑ a_i x_i + c` with `==`, `!=`, `<`, `<=`, `>`, `>=`): an implementation MAY use any sound decision procedure but **MUST be complete for `LinPred` entailment**. Predicates outside the decidable fragment are simply not provable statically — they are not errors at the declaration, but any *obligation* of that shape can only be discharged dynamically (inside `#dynamic`) or must be restructured into the decidable fragment.

#### 16.5.3 Facts: how proofs flow through code

Verification facts have **zero runtime size, no runtime representation, and MUST NOT be stored, passed, or returned** (§15.8.4). A fact `F(P, L)` established at location `L` proves `P` at every point `S` that `L` strictly dominates (rule **Fact-Dominate**: `F(P, L) ∈ Facts`, `L dom S`, `L ≠ S`). Facts are generated by control flow (§15.8.4):

1. `if P { ... }` generates `F(P, _)` at the then-branch entry.
2. `if P { ... } else { ... }` generates `F(NegFact(P), _)` at the else-branch entry, whenever `NegFact(P)` is defined.
3. `if P { return ... }`, `if P { break ... }`, and `if P { continue ... }` generate `F(NegFact(P), _)` on the subsequent fallthrough path, whenever `NegFact(P)` is defined.
4. A satisfied `if ... is` pattern generates pattern facts on the selected-body entry.
5. A runtime check for `P` generates `F(P, _)` after the check.
6. A verified loop invariant generates `F(Inv, _)` after the loop.

`NegFact` is defined for the simple decidable predicates: `NegFact(!P)=P`, `NegFact(a<b)=(a>=b)`, `NegFact(a<=b)=(a>b)`, `NegFact(a>b)=(a<=b)`, `NegFact(a>=b)=(a<b)`, `NegFact(a==b)=(a!=b)`, `NegFact(a!=b)=(a==b)`, and is undefined otherwise. Postcondition facts also flow across calls: a statically-resolved call that returns normally imports the callee's substituted postcondition into the caller's proof context (rule **Fact-Call-Postcondition**), which requires static callee identity, stable argument substitution, and normal return. Type narrowing under an active fact `F(P, L)` refines `typeof(x)` to the refinement `typeof(x) |: {P}`.

This is why the guard-then-call idiom discharges preconditions for free:

```ultraviolet
/// Computes `100 / d`. The precondition forbids a zero divisor.
public procedure reciprocal(d: i64) -> i64 |: d != 0 {
    return 100 / d;
}

/// Returns `100 / d`, or `0` when `d` is zero.
public procedure tryReciprocal(d: i64) -> i64 {
    if d == 0 {
        return 0;
    }
    // On this fall-through path, NegFact(d == 0) = (d != 0) is an active fact,
    // so the precondition of `reciprocal` is proven statically.
    return reciprocal(d);
}
```

The early `return` under `if d == 0` produces the fact `d != 0` on the fall-through path (generation rule 3), which the compiler uses to prove `reciprocal`'s precondition at the call site.

#### 16.5.4 The dynamic check and its panic

When a check is emitted (only in `#dynamic` scope, only when static proof fails), its form is (§15.8.3):

```text
ContractCheck(P, k, s, ρ) = if !P[ρ] { panic(ContractViolation(k, P, s)) }
```

with `k ∈ ContractKind = { Pre, Post, TypeInv, LoopInv, ForeignPre, ForeignPost }`. A check evaluates the predicate under its contract environment `ρ`: `ρ_∅ = ∅` for preconditions and invariants, and `ρ_post = EntryCapture(f, σ_entry) ∪ { @result ↦ v_r }` for postconditions. If the predicate evaluates `false` (rule **Check-False**) or itself panics (**Check-Panic**), the check panics with payload `ContractViolation(k, P, s)`; the runtime diagnostic is `P-SEM-2850` (contract predicate failed at runtime). A successful dynamic check injects the corresponding verification fact afterward, so a dynamically-checked predicate becomes available to subsequent static proofs (§15.8.5; fact-generation rule 5).

The exact insertion points (§15.8.6) place: the precondition check at procedure entry (**Insert-Precondition-Check**); the postcondition check before each return with the returned value (**Insert-Postcondition-Check**); type-invariant checks after construction (**Insert-TypeInv-Construction-Check**), before a public method call (**Insert-TypeInv-PreCall-Check**), and after a mutating (`~!`) method returns (**Insert-TypeInv-PostCall-Check**); and loop-invariant checks before the first iteration (**Insert-LoopInv-Init-Check**) and at the end of each iteration (**Insert-LoopInv-Maintenance-Check**). The same machinery serves refinement-type introduction via **Insert-Refinement-Check** (a refinement `e : T |: {P}` whose proof fails inside `#dynamic` is checked after evaluating `e`).

```ultraviolet
/// Reads an element using a runtime-validated index.
///
/// The `#dynamic` attribute turns the otherwise-unprovable bound check into a
/// runtime contract check instead of a compile error.
public procedure elementChecked(values: [i32; 8], index: usize) -> i32 {
    #dynamic {
        return elementAt(values, index);
    }
}
```

Because `index` is a runtime value the compiler cannot prove `index < 8`; outside `#dynamic` that would be `E-SEM-2801`. The `#dynamic` block opts in to a runtime precondition check that panics (`P-SEM-2850`) on an out-of-range index. (`elementAt` is the procedure from §16.2.3.)

### 16.6 Behavioral Subtyping (§15.9)

When a type implements a class, each implementing procedure's contract must respect the **Liskov substitution principle** relative to the class-declared contract for the same operation (§15.9.4). The obligations are:

- **Preconditions (contravariant):** an implementation **MAY weaken** the class precondition; it **MUST NOT strengthen** it. The verifier statically checks that *the class precondition implies the implementation precondition.* A strengthening implementation is `E-SEM-2803`.
- **Postconditions (covariant):** an implementation **MAY strengthen** the class postcondition; it **MUST NOT weaken** it. The verifier statically checks that *the implementation postcondition implies the class postcondition.* A weakening implementation is `E-SEM-2804`.

These obligations are discharged **statically and entirely**; no runtime checks are generated for behavioral-subtyping obligations (§15.9.4–15.9.6). The rationale is substitutability: any caller that satisfies the class precondition must be acceptable to every implementation, and every implementation's guarantee must be at least as strong as the class promises.

A class method that has no body is **abstract**; a record that implements it provides a matching method **without** the `override` keyword (`override` on an abstract implementation is `E-TYP-2501` — it is reserved for replacing a *concrete* default-bodied class method, where its absence is `E-TYP-2502`). The implementing record must also declare fields satisfying the class's abstract fields.

```ultraviolet
/// A counter protocol that can be advanced.
public class Incrementable {
    value: u64
    maximum: u64

    /// Advances the counter. The class requires headroom below the maximum
    /// and guarantees the value does not decrease.
    procedure bump(~!) -> () |:
        self.value < self.maximum |=
        self.value >= @entry(self.value);
}

/// A saturating counter: advancing at the maximum simply stays there.
public record SaturatingCounter <: Incrementable {
    value: u64
    maximum: u64

    /// Implementation weakens the precondition (it accepts the saturated state,
    /// requiring only `true`) and strengthens the postcondition (it adds the
    /// `value <= maximum` bound the type invariant relies on). No `override`:
    /// `Incrementable::bump` is abstract.
    public procedure bump(~!) -> () |:
        true |=
        (self.value >= @entry(self.value) && self.value <= self.maximum) {
        if self.value < self.maximum {
            self.value = self.value + 1;
        }
        return ();
    }
} |: { self.value <= self.maximum }
```

The abstract class method `bump` carries a contract and is terminated with `;` because it has no body. The implementation's precondition `true` is **weaker** than the class's `self.value < self.maximum` (the class precondition trivially implies `true`), so substitution is sound (`E-SEM-2803` would fire only if the implementation *strengthened* it). The implementation postcondition `self.value >= @entry(self.value) && self.value <= self.maximum` is **stronger** than the class's `self.value >= @entry(self.value)` (the conjunction implies the first conjunct), so it does not weaken the guarantee (`E-SEM-2804` would fire on weakening). The type invariant `|: { self.value <= self.maximum }` supplies the entry-state bound that makes the saturating body satisfy its own postcondition. Both subtyping obligations are proven statically; the `~!` receiver and the `value` / `maximum` fields on `SaturatingCounter` satisfy the class's field requirements.

### 16.7 Idioms & Best Practices

- **Prefer contracts over defensive code.** The style guide is explicit: *prefer precise contracts over broad defensive code* and *prefer contracts over ad hoc runtime checks when the language can express the rule.* A precondition the caller can prove costs nothing at runtime; a hand-written `if x < 0 { return error }` gives the verifier no fact and cannot be discharged statically. Express the rule as `|: x >= 0` instead.
- **Contracts are mandatory where expressible.** Any machine-checkable rule about safety, range, state, ownership, lifetime, authority, or valid sequencing belongs in a contract or invariant — *do not leave machine-checkable rules as comments alone.* Public, cross-module, lifecycle, and FFI surfaces should be especially strict.
- **Put the rule where it belongs.** A rule about a *single value in isolation* (a non-zero divisor, an in-range index) is a **precondition**. A rule relating *output to input* is a **postcondition** with `@result` and `@entry`. A rule that must hold for *every state of a value* is a **type invariant**. A rule that must hold *across every iteration* is a **loop invariant**. A rule that defines a *valid value of a named type* used in many signatures is better factored as a **refinement type** (§14.8 / B.2 `refinement_clause`) than repeated on every parameter.
- **Order members so contracts are visible.** Per the style guide, order type members from most-stable to most-local: constants, fields, **invariants/contracts**, factories/lifecycle, public API, then private helpers. Treat visibility as part of the API contract.
- **Keep `@entry` cheap.** `@entry` only snapshots `Bitcopy` data and copies it bitwise. Snapshot a scalar field (`@entry(self.value)`), not a whole aggregate; if you need to relate a large structure's before/after, capture the scalar summary you actually compare.
- **Use the union / narrowing forms in postconditions.** For procedures returning `A | B`, state the result shape with `@result is A` and read fields with `@result as A`; this is exactly the typing the spec provides and keeps the guarantee precise. Cover every variant so the postcondition is true on all return paths.
- **Let early returns carry the proof.** Structure code so an `if guard { return ... }` establishes the negated fact (`NegFact`) on the fall-through path; downstream preconditions then discharge statically with no `#dynamic` needed.
- **Document the contract in prose too.** Public `///` documentation must cover purpose, important preconditions, important postconditions, ownership or capability expectations, and notable failure modes — the contract clause and the doc comment are complementary, not redundant.
- **Reserve `#dynamic` for genuinely runtime facts.** Use it only when the property truly cannot be known statically (e.g. an index derived from external input). Do not reach for `#dynamic` to silence a provable obligation you have merely written awkwardly; restructure the code into the decidable fragment instead. A `#dynamic` scope that needed no checks warns (`W-CON-0401`).
- **Respect substitutability on overrides.** When implementing a class method, weaken what you require and strengthen what you guarantee — never the reverse.

### 16.8 Foreign Contracts on `extern` Procedures (§23.6)

FFI boundaries are exactly where the style guide demands strict contracts and safe wrappers. Foreign procedures use a parallel contract family with `@` decorator clauses (§23.6.1, Appendix B.13):

```ebnf
ffi_verification_attr ::= "#" ffi_verification_mode
ffi_verification_mode ::= "static" | "dynamic"

foreign_contract      ::= "|:" decorated_identifier("@", "foreign_assumes") "(" predicate_expr ")"
                        | "|:" decorated_identifier("@", "foreign_ensures") "(" ensures_predicate ")"
ensures_predicate     ::= predicate_expr
                        | decorated_identifier("@", "error") ":" predicate_expr
                        | decorated_identifier("@", "null_result") ":" predicate_expr
foreign_contract_clause_list ::= foreign_contract+
```

These attach to a `foreign_procedure` (Appendix B.6 / B.13) after the ordinary `contract_clause?`, and a procedure may carry several. `@foreign_assumes(P)` is a **foreign precondition** — what the caller must guarantee before the unsafe foreign call; its predicate may reference parameter names, literal constants, pure functions and operators, and fields of parameter values, and MUST NOT reference global mutable state, out-of-scope values, or effectful operations. `@foreign_ensures(...)` is a **foreign postcondition** — what the foreign code guarantees on return; its predicate may reference `@result` and parameter names, and may be qualified `@error: P` (holds on failure) or `@null_result: P` (holds when the result is null).

Foreign postconditions classify the call (§23.6.4.2). Let `E` be the list of `@error` predicates and `N` the list of `@null_result` predicates. Then `ErrCond = ⋀ E` if `E ≠ ∅` else `false`; `NullCond = (@result == null)`; and `SuccessCond = ¬ ErrCond`. The obligations are: for each unconditional `P`, `SuccessCond ⇒ P`; for each `@error` predicate `P`, `ErrCond ⇒ P`; and for each `@null_result` predicate `P`, `NullCond ⇒ P`.

`@null_result` is well-formed only when the return type is a nullable pointer — `Ptr<T>@Null`, `*imm T`, or `*mut T` (otherwise `E-SEM-2856`); `@error` is ill-formed on a `()`-returning procedure (`E-SEM-2855`). The verification mode is set by the **`#static`** (default) or **`#dynamic`** attribute (note the leading `#`): `#static` requires a compile-time proof of `@foreign_assumes` and makes `@foreign_ensures` available as downstream assumptions; `#dynamic` inserts `ContractCheck(P, ForeignPre, s, ρ_∅)` immediately before the call and `ContractCheck(P, ForeignPost, s, ρ_foreign_post)` immediately after it, where `ρ_foreign_post = { @result ↦ v_r }` (§23.6.4–23.6.6). For foreign contracts, `#static` is the meaningful default; the ordinary contract system has no `#static` toggle (its default *is* static proof).

```ultraviolet
extern "C" {
    /// Copies `count` bytes from `src` into `dst`.
    ///
    /// Caller obligations: `count` is positive (and, in real code, both buffers
    /// are large enough). Guarantee: returns `dst` on success.
    #static public procedure rawCopy(dst: *mut u8, src: *imm u8, count: usize) -> *mut u8 |:
        @foreign_assumes(count > 0) |:
        @foreign_ensures(@result == dst);
}
```

The `@foreign_assumes(count > 0)` precondition must be proven by the caller at compile time under `#static`; the `@foreign_ensures(@result == dst)` postcondition is imported as an assumption for code after the call. A foreign procedure declaration is terminated by a terminator (here the `;`). A failed `@foreign_assumes` proof is `E-SEM-2850`; an out-of-scope reference is `E-SEM-2852`; an invalid foreign predicate is `E-SEM-2851` (`@foreign_assumes`) or `E-SEM-2853` (`@foreign_ensures`); under `#dynamic`, runtime failures panic with `P-SEM-2860` (precondition) or `P-SEM-2861` (postcondition).

### 16.9 Pitfalls & Diagnostics

The contract, entry, and invariant diagnostics owned by §15.10, the `#dynamic`-attribute diagnostics owned by §9.5.7, and the foreign-contract diagnostics of §23.6.7 are:

| Code         | Severity | Meaning |
| ------------ | -------- | ------- |
| `E-SEM-2801` | Error    | Contract predicate not provable outside a `#dynamic` scope. |
| `E-SEM-2802` | Error    | Impure expression in a contract predicate. |
| `E-SEM-3004` | Error    | Impure expression in a contract clause. |
| `E-SEM-2803` | Error    | Implementation **strengthens** a class precondition (LSP violation). |
| `E-SEM-2804` | Error    | Implementation **weakens** a class postcondition (LSP violation). |
| `E-SEM-2805` | Error    | `@entry()` result type is not `BitcopyType`. |
| `E-SEM-2806` | Error    | `@result` used outside a postcondition. |
| `E-SEM-2807` | Error    | `@entry()` references a parameter whose value is unavailable after binding (e.g. moved). |
| `E-SEM-2808` | Error    | Contract predicate does not have type `bool`. |
| `E-SEM-2820` | Error    | Type invariant violated at construction. |
| `E-SEM-2821` | Error    | Type invariant violated at public entry. |
| `E-SEM-2822` | Error    | Type invariant violated at mutator return. |
| `E-SEM-2823` | Error    | Type invariant violated at private-to-public return. |
| `E-SEM-2824` | Error    | Public mutable field on a type that declares an invariant. |
| `E-SEM-2830` | Error    | Loop invariant not established at initialization. |
| `E-SEM-2831` | Error    | Loop invariant not maintained across an iteration. |
| `E-CON-0415` | Error    | Capability-requiring operation in an `@entry` expression. |
| `E-CON-0416` | Error    | Side-effecting operation in an `@entry` expression. |
| `E-CON-0410` | Error    | `#dynamic` applied directly to a contract clause. |
| `E-CON-0411` | Error    | `#dynamic` applied to a type alias declaration. |
| `E-CON-0412` | Error    | `#dynamic` applied to a field declaration. |
| `W-CON-0401` | Warning  | `#dynamic` present but all proofs succeed statically. |
| `P-SEM-2850` | Panic    | Contract predicate failed at runtime (under `#dynamic`). |
| `E-SEM-2850` | Error    | Cannot prove `@foreign_assumes` predicate. |
| `E-SEM-2851` | Error    | Invalid predicate in a foreign contract. |
| `E-SEM-2852` | Error    | Foreign-contract predicate references an out-of-scope value. |
| `E-SEM-2853` | Error    | Invalid predicate in `@foreign_ensures`. |
| `E-SEM-2854` | Error    | `@result` used in a non-return context (foreign). |
| `E-SEM-2855` | Error    | `@error` predicate on a void-returning foreign procedure. |
| `E-SEM-2856` | Error    | `@null_result` predicate on a non-nullable return type. |
| `P-SEM-2860` | Panic    | Foreign precondition failed at runtime. |
| `P-SEM-2861` | Panic    | Foreign postcondition failed at runtime. |

(Misuse of `override` when implementing a class method — `E-TYP-2501` for `override` on an abstract implementation, `E-TYP-2502` for a missing `override` on a concrete replacement — is owned by the classes chapter but bites directly when writing contracted overrides; see §16.6.)

Common mistakes and how to avoid them:

- **Spelling contracts with English keywords.** There is no `requires` / `ensures` / `invariant` keyword. Use `|:`, `|=`, `@result`, `@entry(...)`, and `|: { ... }` for invariants. Foreign procedures use `@foreign_assumes(...)` / `@foreign_ensures(...)`.
- **Confusing the `|:` forms.** `|: P` is a *contract clause* (precondition `P`); `|: { P }` is an *invariant* (type or loop); `|: @foreign_assumes(P)` is a *foreign* contract; and `type T = U |: { P }` is a *refinement type*. Generic constraints use `<:` bounds, not `|:`.
- **Misplacing the clause.** A contract clause sits **after** the signature and **before** the body. A type invariant sits **after** the closing brace of the declaration body. A loop invariant sits **between** the condition and the body.
- **Malformed receivers.** A method receiver is a shorthand — `~` (const), `~!` (unique/mutable), or `~%` (shared) — **or** an explicit `self : Type`; you cannot combine them (`~! self` is not a receiver). Inside the body the receiver is named `self`; a `~!` receiver is required for any method that mutates `self`, including one whose postcondition compares `self.field` against `@entry(self.field)`.
- **Impure or non-`bool` predicates.** A predicate that calls a capability-taking procedure, mutates, allocates, spawns, or yields is impure (`E-SEM-2802` / `E-SEM-3004`); a predicate that does not have type `bool` is `E-SEM-2808`. Keep predicates to pure comparisons, boolean combinators, field / index access, casts, and pure calls.
- **`@result` / `@entry` misuse.** `@result` outside a postcondition is `E-SEM-2806`. `@entry` over a non-`Bitcopy` type is `E-SEM-2805`; over a moved parameter is `E-SEM-2807`; with side effects is `E-CON-0416`; needing a capability is `E-CON-0415`; referencing anything other than parameters and the receiver is ill-formed. Snapshot scalar `Bitcopy` fields only.
- **Expecting runtime checks by default.** Contracts are **proved**, not checked, unless you are inside a `#dynamic` scope. An unprovable obligation outside `#dynamic` is a compile error (`E-SEM-2801`), not a silent runtime guard. If you genuinely need a runtime check for a value the compiler cannot reason about, wrap that site in `#dynamic` — but first consider whether restructuring (guards that generate facts, or a refinement type) makes it statically provable.
- **Public mutable fields under an invariant.** A type with `|: { ... }` must not expose public mutable fields (`E-SEM-2824`); route all mutation through receiver-taking methods so the invariant is re-checked at entry and at mutator return.
- **Breaking substitutability on overrides.** When implementing a class method, do not tighten the precondition (`E-SEM-2803`) or loosen the postcondition (`E-SEM-2804`). Weaken what you require; strengthen what you guarantee. Do not add `override` to an abstract-method implementation (`E-TYP-2501`).
