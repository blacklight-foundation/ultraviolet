## 20. Permissions & Binding State

Permissions are the type-level qualifiers that govern who may read, who may write, and how many simultaneous references may exist to the data a binding names. Together with the binding *activity* state machine, they give the Ultraviolet compiler everything it needs to reason about mutation and aliasing *locally* — at a single call site, in a single signature, without whole-program analysis. This chapter defines the three permission regimes (§10.1), the aliasing and exclusivity rules that follow from them (§10.2), the live/inactive activity states a `unique` binding moves through (§10.3), and the admissibility relation that decides which caller permissions may satisfy a required receiver or non-consuming parameter permission (§10.4).

This chapter is closely tied to several others. The receiver-shorthand tokens are parsed by the method parser described in **Chapter 5 (AST)** and **Chapter 15 (Parsing)**; the key-mediated synchronization that `shared` access performs is defined in **Chapter 19 (Key System)**; permission-qualified layout/ABI is defined in **Chapter 24 (Lowering & ABI)**; and general subtyping of permission-qualified types lives in **Chapter 8 (Type System Core)**. Value movement (`move`/`copy`) and the consuming-vs-non-consuming distinction connect to the binding and ownership material in the value-use chapters.

### 20.1 Permission Forms (§10.1)

#### 20.1.1 Exact Syntax

A permission is one of exactly three keywords. The receiver-position shorthand is one of exactly three operator tokens. These are the *only* spellings; there is no `mut`, no `ref`, no `owned`, and no permission keyword beyond the three below.

```ebnf
permission         ::= "const" | "unique" | "shared"
receiver_shorthand ::= "~" | "~!" | "~%"
```

Permission qualifiers appear at the head of a type. From the Type Grammar (Appendix B.2):

```ebnf
type                ::= permission? non_permission_type refinement_clause?
non_permission_type ::= union_type | non_union_type
permission          ::= "const" | "unique" | "shared"
```

So a permission, when written, is the *leftmost* token of a type, sitting before the base type and before any refinement clause (`|: { ... }`). A type may also omit the permission entirely.

#### 20.1.2 Parsing

Permission tokens are parsed in two distinct positions by two distinct routines:

- In **type position**, the optional leading permission is consumed by `ParsePermOpt`.
- In **receiver position** (the first slot of a method's parameter list), the shorthand is consumed by `ParseReceiver`, whose canonical token-gated rules live in §15.2.2. §10.1.2 owns only the permission-specific token inventory consumed by those rules.

The receiver shorthand maps one-to-one onto a receiver permission. The verbatim parser rules (§15.2.2) are:

```text
(Parse-Receiver-Short-Const)
IsOp(Tok(P), "~")
──────────────────────────────────────────────────────────────
Γ ⊢ ParseReceiver(P) ⇓ (Advance(P), ReceiverShorthand(`const`))

(Parse-Receiver-Short-Unique)
IsOp(Tok(P), "~!")
──────────────────────────────────────────────────────────────
Γ ⊢ ParseReceiver(P) ⇓ (Advance(P), ReceiverShorthand(`unique`))

(Parse-Receiver-Short-Shared)
IsOp(Tok(P), "~%")
──────────────────────────────────────────────────────────────
Γ ⊢ ParseReceiver(P) ⇓ (Advance(P), ReceiverShorthand(`shared`))
```

The mapping is therefore:

| Receiver shorthand | Receiver permission |
| :----------------- | :------------------ |
| `~`                | `const`             |
| `~%`               | `shared`            |
| `~!`               | `unique`            |

A receiver may also be written explicitly as `self` with a full permission-qualified type. From Appendix B.6:

```ebnf
receiver           ::= receiver_shorthand | explicit_receiver
receiver_shorthand ::= "~" | "~!" | "~%"
explicit_receiver  ::= param_mode? "self" ":" type
param_mode         ::= "move"
```

The explicit form is parsed by `(Parse-Receiver-Explicit)` (§15.2.2). The shorthand forms are the idiomatic choice; the explicit form is used only when you also need a `move` mode on the receiver, or when writing the type out aids review.

#### 20.1.3 AST Representation

Permission syntax lowers to the following AST forms:

- `Perm` ranges over `{const, unique, shared}`.
- `TypePerm(perm, base)` — a permission-qualified type, where `perm ∈ Perm` and `base` is the *unqualified* base type.
- `ReceiverShorthand(perm)` — a receiver carrying a permission.

A permission-qualified type is represented as `TypePerm(P, T)` where `P ∈ Perm` and `T` is the unqualified base type. The method receiver AST (§15.2.3) is:

```text
Receiver ∈ {ReceiverShorthand(perm), ReceiverExplicit(mode, type)}
perm     ∈ {const, unique, shared}
mode     ∈ {move, ⊥}
```

#### 20.1.4 Static Semantics: the three regimes

A **permission** is a type qualifier governing access, mutation, and aliasing of the data referenced by a binding. **When no permission is specified, `const` is the default.** This is the single most important default to internalize: an unqualified type is a `const` type.

`const`, `shared`, and `unique` are distinct **permission regimes**. They are *not* ordered by implicit subtype or coercion — there is no silent widening or narrowing among them. The defining table:

| Permission | Read | Write | Aliasing  | Synchronization |
| :--------- | :--- | :---- | :-------- | :-------------- |
| `const`    | Yes  | No    | Unlimited | N/A             |
| `shared`   | Yes  | Yes   | Aliasable | Key-mediated    |
| `unique`   | Yes  | Yes   | Exclusive | None            |

Regime-specific constraints:

- `const` forbids mutation through the qualified path. Reading is always allowed; aliasing is unlimited because read-only data cannot race.
- `unique` grants exclusive read-write access. It does **not** imply cleanup responsibility — `unique` is about exclusivity, not ownership/destruction.
- `shared` grants synchronized read-write access. It also does **not** imply cleanup responsibility. Every actual access goes through the key system.

For `shared` specifically, the permitted operations and the key mode each one acquires are:

| Operation                   | Permitted | Key Mode  |
| :-------------------------- | :-------- | :-------- |
| Field read                  | Yes       | Read key  |
| Field mutation              | Yes       | Write key |
| Method call (`~` receiver)  | Yes       | Read key  |
| Method call (`~%` receiver) | Yes       | Write key |
| Method call (`~!` receiver) | No        | N/A       |

Note the last row: a `shared` path can never invoke a `~!` (unique-receiver) method, because providing exclusive access from an aliasable reference is impossible. This is enforced by admissibility (§20.4), not by an ad-hoc rule.

#### 20.1.5 Dynamic Semantics

Permissions do **not** change the represented value of the underlying object. They are a static discipline with one runtime consequence, isolated to `shared`:

- `const` introduces no additional runtime action.
- `unique` introduces no implicit cleanup transfer.
- `shared` access performs implicit key acquisition and release as defined by the key system in **Chapter 19**.

#### 20.1.6 Lowering

Permission qualifiers do not alter value layout. `layout`, `sizeof`, and `alignof` for `TypePerm(p, T)` are identical to those of `T`, defined by `Layout-Perm`, `Size-Perm`, and `Align-Perm` in §24.2.2:

```text
(Layout-Perm)            (Size-Perm)              (Align-Perm)
Γ ⊢ layout(T) ⇓ L        Γ ⊢ sizeof(T) = n        Γ ⊢ alignof(T) = a
──────────────────────   ──────────────────────   ────────────────────────
Γ ⊢ layout(TypePerm(p,T)) Γ ⊢ sizeof(TypePerm(p,T)) Γ ⊢ alignof(TypePerm(p,T))
        ⇓ L                       = n                       = a
```

`ValueBits(TypePerm(p, T), v) = bits ⇔ ValueBits(T, v) = bits`. ABI and LLVM lowering for permission-qualified types is governed by the shared lowering and ABI framework in **Chapter 24**. In short: a permission is a compile-time gate, never a runtime tag and never a layout change.

#### 20.1.7 Worked example

The following shows the three regimes appearing in parameter and field positions, and the three receiver shorthands on methods. (`~` reads; `~%` mutates through the key system; `~!` requires exclusive access.) Note that a unit-returning procedure is written `-> ()`.

```ultraviolet
/// A frame counter with three method receiver permissions.
public record FrameCounter {
    public count: u64

    /// Read-only access. `~` is the `const` receiver.
    public procedure current(~) -> u64 {
        return self.count
    }

    /// Synchronized mutation. `~%` is the `shared` receiver; the write
    /// acquires a write key on `self` (Chapter 19).
    public procedure bump(~%) -> () {
        self.count += 1
    }

    /// Exclusive reset. `~!` is the `unique` receiver and demands
    /// exclusive, unaliased access to `self`.
    public procedure reset(~!) -> () {
        self.count = 0
    }
}

/// A parameter typed `const u64` (explicit), and one defaulted to `const`.
procedure describe(explicit_const: const u64, defaulted: u64) -> u64 {
    // Both parameters are `const`: readable, never writable through these paths.
    return explicit_const + defaulted
}
```

### 20.2 Alias and Exclusivity Rules (§10.2)

This section adds no new concrete syntax, no new parsing, and no new AST forms beyond the permission-qualified types and place/path forms already defined. It is pure static semantics over *paths*.

#### 20.2.1 Aliasing and the exclusivity invariant

Two paths **alias** when they refer to overlapping storage:

```text
aliases(p_1, p_2) ⇔ storage(p_1) ∩ storage(p_2) ≠ ∅
```

The **Exclusivity Invariant** is the heart of `unique`:

```text
∀ p_1, p_2 ∈ Paths. (perm(p_1) = `unique` ∧ overlaps(p_1, p_2)) ⇒ p_1 = p_2
```

In words: if any path has `unique` permission, the only path that may overlap its storage is itself. No second path — of *any* permission — may touch storage that a `unique` path covers. This is exactly what makes `unique` sound for local reasoning: a `unique T` is the *only* way to reach that storage, so any mutation through it cannot be observed or invalidated through some other reference.

#### 20.2.2 The Coexistence Matrix

When data already carries one permission, which additional permissions may be introduced over (overlapping) storage is fixed by the **Coexistence Matrix**:

| Existing Permission | May Add `unique` | May Add `shared` | May Add `const` |
| :------------------ | :--------------- | :--------------- | :-------------- |
| `unique`            | No               | No               | No              |
| `shared`            | No               | Yes              | Yes             |
| `const`             | No               | No               | Yes             |

Reading the rows:

- **Existing `unique`:** nothing may be added. The exclusivity invariant forbids any overlapping path at all. This is the strongest guarantee.
- **Existing `shared`:** you may add more `shared` paths (aliasing is allowed and serialized by keys) and `const` paths (read-only observers are safe), but never `unique` (you cannot promise exclusivity over storage that is already aliasable).
- **Existing `const`:** you may add more `const` paths (read-only data can be observed without limit), but never `shared` or `unique` — both of those imply write capability, and granting write capability over data that already has live read-only observers would break those observers' read-only assumption.

Note the diagonal asymmetry: `const` may coexist with `const`, and `shared` may coexist with `shared` and `const`, but `unique` coexists with nothing — not even another `unique`.

#### 20.2.3 Dynamic semantics and lowering

This section introduces no additional runtime mechanism beyond the key-mediated synchronization required for `shared` accesses (Chapter 19) and the scope-based binding activity rules in §20.3. It introduces no lowering rules beyond the permission-qualified layout/ABI of §20.1.6.

#### 20.2.4 Worked example

```ultraviolet
/// Two `const` views of the same data may coexist (const + const row of the
/// coexistence matrix). Both paths are read-only, so unlimited aliasing is
/// permitted; neither call site can mutate through `left` or `right`.
procedure sumPair(left: const u32, right: const u32) -> u32 {
    return left + right
}

/// A `unique` parameter is the *only* path to its storage. The exclusivity
/// invariant guarantees no overlapping `const`, `shared`, or `unique` path
/// can reach the same bytes while this one is live.
procedure zeroFirst(buffer: unique [u8]) -> () {
    // Free to mutate through `buffer`: nothing else can observe these writes.
    buffer[0] = 0
}
```

### 20.3 Binding Activity States (§10.3)

Binding activity is a **semantic state machine over bindings**. It has no concrete syntax, no parsing rule, and **no dedicated AST node** — it is enforced entirely by the static semantics and by the evaluation contexts that consume those judgments.

#### 20.3.1 The two states

A binding `b` with **`unique`** permission exists in exactly one of two states:

| State    | Definition                                              | Operations Permitted        |
| :------- | :------------------------------------------------------ | :-------------------------- |
| Active   | No suspended admissible uses of `b` are live            | Read, write, move           |
| Inactive | A non-consuming admissible use of `b` is currently live | No direct operations on `b` |

While **Active**, the unique binding behaves normally: you may read it, write through it, or move it. While **Inactive**, *direct* use of `b` is suspended — because some non-consuming admissible use of `b` (for example, passing `b` to a procedure that borrows it non-consumingly) is currently live, and that live use holds the exclusive path.

#### 20.3.2 Transition rules

The two transitions are deterministic and scope-driven:

```text
(Inactive-Enter)
b : `unique` T    b is Active    non-consuming admissible use of b begins
──────────────────────────────────────────────────────────────────────────────
b becomes Inactive

(Inactive-Exit)
b is Inactive    all live non-consuming admissible uses of b end
──────────────────────────────────────────────────────────────────────────────
b becomes Active with `unique` permission preserved
```

When a non-consuming admissible use of an Active `unique` binding begins, the binding becomes **Inactive**. When *all* live non-consuming uses end, it returns to **Active** — and crucially, the `unique` permission is **preserved** across the round trip. The binding does not silently degrade to `shared` or `const`.

#### 20.3.3 Dynamic semantics

During the inactive period, the original `unique` binding **MUST NOT** be read, written, or moved. Entering or exiting the inactive state does **not** create a `shared` or `const` alias; it merely *suspends* direct use of the `unique` binding while the admissible use remains live. The transition back to `Active` occurs **deterministically** when the admissible-use scope ends — there is no nondeterminism and no runtime bookkeeping; the lifetime of the borrowing use is a static, lexical fact.

This is the mechanism that lets `unique` be both exclusive *and* usable: you can lend a `unique` binding to a callee non-consumingly, the binding goes Inactive for exactly the duration of that lend, and it comes back Active afterward with its full `unique` permission intact. Lowering introduces nothing here (§10.3.6); the activity machine is purely a static check.

#### 20.3.4 Worked example

```ultraviolet
/// Reads through a non-consuming `const`-required parameter. Calling this
/// with a `unique` argument lends it: the caller's binding goes Inactive
/// for the duration of the call, then returns to Active.
procedure peekFirst(data: const [u8]) -> u8 {
    return data[0]
}

procedure rotate() -> () {
    var handle: unique [u8] = makeBuffer()

    // `handle` is Active here: read, write, and move are all permitted.
    handle[0] = 7

    // During this call `handle` is Inactive (a non-consuming admissible use
    // is live). The borrow begins and ends with the call; direct use of
    // `handle` while that borrow is live would be rejected.
    let first: u8 = peekFirst(handle)

    // The non-consuming use has ended, so `handle` is Active again with
    // `unique` preserved. Writing through it is permitted once more.
    handle[1] = first
}
```

### 20.4 Permission Admissibility (§10.4)

Admissibility answers a precise question: *which caller permissions satisfy a required receiver or non-consuming parameter permission?* It introduces no new syntax, parsing, or AST forms — it is defined over the existing `TypePerm(perm, base)` and `ReceiverShorthand(perm)` forms. Critically, admissibility **is not subtyping and is not coercion**. It decides whether a call is *allowed*; it never rewrites the static type of the caller's expression.

#### 20.4.1 The `PermAdmits` relation

```text
PermAdmitsJudg = {PermAdmits}
```

`PermAdmits(P_caller, P_req)` holds for **exactly** these six pairs and no others:

- `PermAdmits(const, const)`
- `PermAdmits(shared, const)`
- `PermAdmits(shared, shared)`
- `PermAdmits(unique, const)`
- `PermAdmits(unique, shared)`
- `PermAdmits(unique, unique)`

As a matrix (rows = caller permission, columns = required permission):

| Caller \ Required | `const` | `shared` | `unique` |
| :---------------- | :------ | :------- | :------- |
| `const`           | Yes     | No       | No       |
| `shared`          | Yes     | Yes      | No       |
| `unique`          | Yes     | Yes      | Yes      |

The intuition is "a stronger caller permission can satisfy a weaker requirement, never the reverse": `unique` satisfies everything, `shared` satisfies `shared` and `const`, and `const` satisfies only `const`. But this is *not* a subtype lattice — it is a use-site gate with two firm rules:

- `PermAdmits` **MUST** be used for receiver checking, key-mediated operation checking, and non-consuming argument passing.
- `PermAdmits` **MUST NOT** rewrite the static type of the caller expression. The caller's `unique x` stays `unique x` even when passed to a `const`-required parameter; it does not become `const`.

#### 20.4.2 Method receiver permissions

A method whose receiver permission is `P_method` is callable through a path with permission `P_caller` iff `PermAdmits(P_caller, P_method)`. Specializing the matrix to the receiver shorthands:

| Caller Permission | May Call `~` | May Call `~%` | May Call `~!` |
| :---------------- | :----------- | :------------ | :------------ |
| `const`           | Yes          | No            | No            |
| `shared`          | Yes          | Yes           | No            |
| `unique`          | Yes          | Yes           | Yes           |

Recall `~` = `const` receiver, `~%` = `shared` receiver, `~!` = `unique` receiver. So a `const` path may only call read-only (`~`) methods; a `shared` path may call `~` and `~%` (the synchronized mutators) but never `~!`; and a `unique` path may call all three. This is precisely why, in §20.1.4, the `shared` operation table marked "Method call (`~!` receiver)" as *not permitted*: it falls out of `PermAdmits(shared, unique)` being false.

#### 20.4.3 Constraints

Two normative constraints close off the loopholes:

1. **No implicit conversion, coercion, rebinding, or alias creation** from `unique T` to `shared T` or `const T` exists. You cannot launder a `unique` into a `shared` or `const` by assignment, by passing it, or by any implicit step.
2. General subtyping of permission-qualified types is defined by the type-system core in **Chapter 8** and **requires permission equality**. Subtyping never crosses regimes; admissibility (this section) is the *only* relation that lets a stronger permission satisfy a weaker requirement, and it does so without changing types.

#### 20.4.4 Dynamic semantics and lowering

Permission admissibility does not rewrite runtime values or runtime type identity. For `shared` operations, admissibility merely determines whether the operation may *proceed* to key-mediated access; the runtime key behavior itself is defined by **Chapter 19**. Admissibility is a **static gate only** — it introduces no distinct runtime representation and no separate ABI form.

#### 20.4.5 How permissions enable local reasoning

Put the pieces together and the payoff is local reasoning at every signature:

- A `const` parameter promises the callee will not mutate through that path, and (by the coexistence matrix) tolerates unlimited read-only aliasing — so the caller knows its data is untouched.
- A `unique` parameter promises the callee holds the *only* path to that storage (exclusivity invariant), so the callee may mutate freely with no fear of hidden aliases, and the caller's binding is simply Inactive (then Active again) for the call's duration.
- A `shared` parameter promises mutation is possible but always serialized through keys, so concurrent access is well-defined.

Because all three promises are visible *in the signature*, a reader (human or compiler) can verify each call against `PermAdmits` and the activity rules without inspecting the callee's body or the rest of the program. This is the whole point of the permission system, and it is why the style guide directs you to "use the type system… before reaching for weaker runtime-only validation" and to "keep authority narrow."

#### 20.4.6 Worked example

```ultraviolet
public record Ledger {
    public total: i64

    /// `~` (const) receiver: callable from any path permission.
    public procedure total(~) -> i64 {
        return self.total
    }

    /// `~%` (shared) receiver: callable from `shared` and `unique` paths.
    public procedure credit(~%, amount: i64) -> () {
        self.total += amount
    }

    /// `~!` (unique) receiver: callable only from a `unique` path.
    public procedure clear(~!) -> () {
        self.total = 0
    }
}

procedure settle(audit: const Ledger, pool: shared Ledger, scratch: unique Ledger) -> i64 {
    let observed: i64 = audit.total()   // const path  -> `~` : admitted
    pool.credit(5)                       // shared path -> `~%`: admitted
    scratch.clear()                      // unique path -> `~!`: admitted
    return observed
}
```

In `settle`, each call is gated by `PermAdmits(P_caller, P_method)`. Calling `pool.clear()` would be rejected — `PermAdmits(shared, unique)` is false (`E-TYP-1605`) — and calling `audit.credit(5)` would be rejected for the same reason (`PermAdmits(const, shared)` is false).

### 20.5 Permissions in Parameter Signatures and `move`

Permissions are orthogonal to the `move` *parameter mode*. A permission qualifies the *type*; `move` (the `param_mode`) marks a *consuming* parameter that takes ownership. From Appendix B.6:

```ebnf
param      ::= param_mode? identifier ":" type
param_mode ::= "move"
```

A non-`move` parameter is **non-consuming**: the call borrows the argument, and `PermAdmits` governs whether the caller's permission satisfies the parameter's required permission. A `move` parameter is **consuming**: it transfers ownership, and the argument must be a place expression passed with `move` (or, for `Bitcopy` values, an explicit `copy`). The argument-side modifiers come from the expression grammar (Appendix B.3):

```ebnf
argument ::= ("move" | "copy")? expression
```

The interactions to remember, with their diagnostics and typing-rule names:

- A non-`move` argument with source provenance **must be a place expression** — otherwise `E-TYP-1603` (`Call-Arg-NotPlace`).
- Omitting `move`/`copy` on a *consuming* parameter is `E-SEM-2534` (`Call-Move-Missing`).
- Supplying `move` to a *non-consuming* parameter is `E-SEM-2535` (`Call-Move-Unexpected`).

```ultraviolet
/// `slot` is a *consuming* (`move`) parameter typed `unique`. Ownership of
/// the buffer transfers into `consume`.
procedure consume(move slot: unique [u8]) -> () {
    slot[0] = 0
}

procedure handOff() -> () {
    var owned: unique [u8] = makeBuffer()
    consume(move owned)   // `move` required: `slot` is a consuming parameter
}
```

### Idioms & Best Practices

- **Let `const` be the default; write it only for emphasis.** An unqualified type *is* `const`. Per the style guide ("keep authority narrow — pass only the capabilities and data that are actually used"), default every parameter to read-only and escalate to `shared` or `unique` only where mutation is genuinely required.
- **Reach for `unique` before `shared`.** `unique` gives the strongest local-reasoning guarantee (the exclusivity invariant) and costs *nothing* at runtime — no keys, no synchronization. Use `shared` only when the data must actually be aliased and mutated by more than one path.
- **Match the receiver shorthand to intent.** Use `~` for query/observer methods, `~%` for synchronized mutators on shared data, and `~!` for operations that must own the object exclusively (resets, finalizers, in-place reorganizations). Boolean-returning observers should additionally follow the predicate-`camelCase` naming (`isReady`, `hasFocus`).
- **Prefer the shorthand over an explicit `self: T` receiver.** Write `~`, `~%`, `~!` rather than a `self`-typed `explicit_receiver` unless you also need a `move` receiver mode or the explicit type genuinely aids review.
- **Lend, don't downgrade.** To let a callee read a `unique` binding, pass it to a `const`-required non-consuming parameter. The activity machine makes the binding Inactive for exactly the call and restores it to Active with `unique` preserved — there is no need (and no way) to "convert" it to `const`.
- **Express ownership and access in the signature, not in comments.** The style guide is explicit: "if a rule about… ownership, lifetime, authority… can be expressed with contracts or invariants, express it in code." Permissions and receiver shorthands *are* that expression — they belong in the type, not in a `//` note.
- **Document the permission contract on public APIs.** All public procedures, methods, and transitions require `///` documentation that covers ownership or capability expectations; state plainly whether a receiver is `~`, `~%`, or `~!` and what a `unique`/`move` parameter consumes.

### Pitfalls & Diagnostics

- **Mutating through a `const` path → `E-TYP-1601`.** Any write (field mutation, compound assignment, or a `~%`/`~!` method call) through a `const`-qualified path is a compile-time error. The fix is to require `shared` or `unique` at the relevant parameter/receiver — not to silently widen.
- **Violating `unique` exclusivity → `E-TYP-1602`.** Creating an overlapping path to `unique` storage, or using a `unique` binding while it is **Inactive** (during a live non-consuming borrow), trips the `unique` exclusion check. Remember: while a borrow is live, the original binding *must not* be read, written, or moved.
- **Receiver permission incompatible with caller → `E-TYP-1605`** (`MethodCall-RecvPerm-Err`). This is `PermAdmits(P_caller, P_method)` failing — for example a `const` path calling `~%`/`~!`, or a `shared` path calling `~!`. The matrix in §20.4.2 is the authority; do not attempt to "cast" the caller's permission, because no such conversion exists (Constraint 1).
- **Shared-place mutation with no valid key context → `E-TYP-1604`.** A `shared` write must form a valid key-mediated write context; if it cannot, this fires. Ensure the mutation sits where a write key on the place can be acquired (Chapter 19).
- **`move` on a non-consuming parameter → `E-SEM-2535`** (`Call-Move-Unexpected`); **missing `move`/`copy` on a consuming parameter → `E-SEM-2534`** (`Call-Move-Missing`); **non-`move` argument with provenance that isn't a place → `E-TYP-1603`** (`Call-Arg-NotPlace`). Keep the parameter mode (`move`) and the type permission (`const`/`shared`/`unique`) conceptually separate: the first decides *ownership transfer*, the second decides *access and aliasing*.
- **Expecting implicit `unique → shared`/`const`.** There is no implicit conversion, coercion, rebinding, or alias creation from `unique T` to `shared T` or `const T` (Constraint 1), and subtyping requires permission *equality* (Constraint 2). If you need a weaker view, pass through a non-consuming parameter whose required permission `PermAdmits` accepts — admissibility never changes the caller's static type.
- **`shared $Cl` with a non-`~` dynamic method → `E-CON-0083` (ill-formed).** A `shared` dynamic-class value is well-formed only if every vtable-eligible procedure (including inherited procedures) uses the `~` (const) receiver. If any method requires `~%` (`shared`) or `~!` (`unique`) receiver permission, `shared $Cl` is rejected. Design dynamic interfaces intended for `shared` use with `~` receivers (see Chapter 19).
