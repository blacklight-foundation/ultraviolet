## 23. The Key System: Shared-Memory Concurrency

Ultraviolet has no runtime mutexes, no `lock`/`unlock` calls, and no library-level synchronization primitives in its safe core. Instead, the right to read or write a piece of `shared` data is a *key*, and keys are governed by the static type system. Where another language would write `mutex.lock()`, accept the runtime cost, and hope every developer remembers to pair it with `unlock()`, Ultraviolet makes the access itself prove — at compile time — that an incompatible key is not simultaneously held. A `shared` access that cannot be covered by a valid key context is a compile error, not a data race.

This chapter specifies the entire Key System: key paths (§19.1), key acquisition blocks (§19.2), conflict detection (§19.3), nested release (§19.4), speculative execution (§19.5), dynamic key verification (§19.6), and memory ordering (§19.7). The key system is the bridge between the permission system (the `const`/`unique`/`shared` qualifiers introduced in the chapter on permissions) and the structured-parallelism constructs (`parallel`, `dispatch`, `spawn`) covered in later chapters. Read this chapter alongside *The Permission System* (for what `shared` means as a type qualifier) and *Structured Parallelism* (for the task constructs that make concurrent key acquisition observable).

The defining property: **every `shared` access is key-covered, and key transfer establishes happens-before.** Programs whose safe fragment the compiler accepts are *data-race-free* by construction (§19.7.5, `Data-Race-Freedom`).

> Note on method-call syntax in this chapter. Ultraviolet's method-call operator is `~>` (`postfix_suffix ::= … | "~>" identifier "(" argument_list? ")"`), not `.`. The `.` operator is field projection and tuple indexing only. Every method call in this chapter's examples therefore uses `~>`. To keep examples verifiable against the specification, the worked examples never invoke an unspecified library helper (for instance, no `~>len()` on a slice); element counts are passed explicitly or array lengths are fixed at the type level.

### 23.1 Why Keys Replace Runtime Mutexes

A runtime mutex is an object you hold a reference to and call methods on. It has three weaknesses the key system eliminates:

1. **It is not tied to the data.** Nothing forces the mutex you locked to be the one that actually guards the field you are about to write. The key system roots every key in the *key path* of the actual `shared` place being accessed, so the protected resource and the protection are the same thing.
2. **It is not statically checked.** Forgetting to lock, locking the wrong object, or holding two locks in inconsistent orders are runtime bugs. The key system turns all of these into compile-time diagnostics in the `E-CON-*` family.
3. **It always costs something at runtime.** A mutex synchronizes even when the compiler could prove no two tasks ever touch the data concurrently. The key system's *dynamic key verification* (§19.6) omits runtime synchronization wherever it can construct a sound static safety proof, and only emits runtime synchronization where it genuinely must.

Keys are *scope-bound*: a key acquired in scope `S` is released exactly when `S` exits — on normal completion, `return`, `break`, `continue`, panic propagation, or task cancellation (§19.2.5). There is no `unlock`, and there is no way to leak a held key past its scope. The compiler enforces this with the escape diagnostics of §19.2.7.

Two acquisition styles exist, and both are specified here:

- **Implicit acquisition.** An ordinary `shared` access that is not already covered by a held key establishes an implicit acquisition for exactly that access (§19.1.6, **(Lower-KeyAccess-Uncovered)**). You do not have to write a key block to touch `shared` data; being outside an explicit key block does not by itself make an ordinary `shared` access invalid (§19.2.4).
- **Explicit key blocks.** A `%read`/`%write` block acquires keys to a stated set of paths for the duration of a body, so that a sequence of accesses shares one coherent key context (§19.2). This is how you make a multi-step read-modify-write atomic.

### 23.2 Key Paths (§19.1)

A *key path* is the static address of the `shared` storage an access touches. It is what a key locks. Key analysis is performed iff the path root has `shared` permission; accesses rooted in `const` or `unique` data require no keys at all (§19.1.4).

#### 23.2.1 Key-Path Syntax

The surface grammar for a key path is reproduced verbatim from §19.1.1 (identical in Appendix B.9):

```ebnf
key_path_expr ::= key_root key_seg*
key_root      ::= identifier
key_seg       ::= "." key_field | "[" key_index "]"
key_field     ::= key_marker? identifier
key_index     ::= key_marker? expression
key_marker    ::= "#"
```

A key path is a root identifier followed by zero or more segments. Each segment is either a field projection (`.name`) or an index (`[expr]`). Either kind of segment may be prefixed by the coarsening marker `#` (§23.2.4). The `#` marker is parsed by `ParseKeyMarkerOpt` and validated for well-formedness afterward (§19.1.2, §19.1.4).

#### 23.2.2 Path Well-Formedness

From §19.1.4:

- A key path is well-formed iff each segment is valid for the type of the preceding segment. Indexing requires an array/slice type; field projection requires a record (or state) type.
- A key path MUST contain at most one `#` marker. Two markers in one path expression is `E-CON-0003`.
- The `#` marker is only meaningful on a path rooted in `shared` data. A `#` on a non-`shared` path is `E-CON-0002`.
- `#` immediately before a method name is `E-CON-0030`; `#` on a field of a non-record type is `E-CON-0033`.

#### 23.2.3 Path Root Extraction and Boundaries

The *root* of a place expression determines whether key analysis applies and where the key is anchored. From §19.1.4, `Root(e)` is defined recursively:

```text
Root(e) =
 x                 if e = x
 Root(e')          if e = e'.f
 Root(e')          if e = e'[i]
 Root(e')          if e = e' ~> m(...)
 ⊥_boundary        if e = (*e')
```

Field and index projections preserve the root; a pointer dereference `(*e')` introduces a *key boundary* `⊥_boundary`. The key path therefore truncates at a pointer dereference. From the **KeyPath Formation** rules of §19.1.4: if `Root(e) = x` then `KeyPath(e) = x.p_2 … p_n` truncated by any type-level boundary; if `Root(e) = ⊥_boundary` and `e = (*e').p_2 … p_n` then `KeyPath(e) = id(*e').p_2 … p_n`. For `(*e').p` where `e' : shared Ptr<T>@Valid`, dereferencing `e'` requires a `Read`-mode key to `KeyPath(e')`, and the access to `.p` uses a *fresh* key rooted at the runtime identity `id(*e')`. This is the only place runtime object identity enters static key paths.

If the compiler cannot derive a key-path root for a `shared` access, that is `E-CON-0034`.

A field whose declaration is marked with `#` establishes a *permanent* key boundary: key paths truncate at that field (§19.2.4). In a class declaration, this is the `key_boundary` production (Appendix B.6): `abstract_field ::= attribute_list? visibility? key_boundary? identifier ":" type` where `key_boundary ::= "#"`.

#### 23.2.4 Key Coarsening with `#`

The `#` marker in a *path expression* sets the granularity at which the key is acquired. The key is acquired at the marked position and covers all subsequent segments (§19.2.4, **Key Coarsening**):

```text
(K-Coarsen-Inline)
P = p_1 … p_(k-1).#p_k … p_n    Γ ⊢ P : shared T    M = RequiredMode(P)
────────────────────────────────────────────────────────────────────────
AcquireKey(p_1 … p_k, M, Γ_keys)
```

So `world.#chunks[i].voxels[j]` acquires one key covering all of `world.chunks` rather than a fine-grained key per voxel. Coarsening trades concurrency for fewer acquisitions — useful in tight loops where fine-grained keys would thrash (the `W-CON-0001` performance hint flags exactly this situation). If a `#` lands exactly on a position that already coincides with a type-level key boundary, the marker is redundant and the compiler emits `W-CON-0003`.

#### 23.2.5 `shared` Dynamic Class Objects

A dynamic class object `$Cl` MAY be qualified `shared` only if *every* vtable-eligible procedure — including inherited ones — has a `const` receiver (`~`). From §19.1.4:

```text
(K-Witness-Shared-WF)
∀ m ∈ DynMethods(Cl). m.receiver = ~
──────────────────────────────────
Γ ⊢ shared $Cl wf
```

If any dynamically dispatched method needs a `shared` (`~%`) or `unique` (`~!`) receiver, `shared $Cl` is ill-formed (`E-CON-0083`). For a vtable call `e~>m(args)` on `e : shared $Cl`, the key mode is `Read` and the key path is `Root(e)` (§19.1.5):

```text
KeyPath(e~>m(...)) = Root(e)
```

#### 23.2.6 Worked Example — Key Paths and Coarsening

```ultraviolet
/// A voxel chunk; `density` and `material` are the per-cell shared state.
public record Chunk {
    public density: [f32]
    public material: [u8]
}

/// A world holds many chunks. Touching `world.chunks[i].density[j]`
/// forms the key path `world.chunks[i].density[j]`.
public record World {
    public chunks: [Chunk]
}

/// Read one density cell. The argument is `shared`, so an ordinary read of
/// `w.chunks[i].density[j]` forms a fine-grained `Read` key path. The read
/// is uncovered, so it acquires its own key implicitly for the access.
public procedure sampleDensity(w: shared World, i: usize, j: usize) -> f32 {
    return w.chunks[i].density[j]
}

/// Coarsen the key to cover the whole `chunks` array for a batched scan.
/// `w.#chunks[i]...` acquires one key at `w.chunks`. The element count is
/// passed explicitly so the example relies on no unspecified slice helper.
public procedure scanChunk(w: shared World, i: usize, cell_count: usize) -> f32 {
    var total: f32 = 0.0
    %read w.#chunks {
        var j: usize = 0
        loop j < cell_count {
            total += w.chunks[i].density[j]
            j += 1
        }
    }
    return total
}
```

### 23.3 Key Acquisition Blocks (§19.2)

A key block names a set of paths, a mode, and a body, and holds keys to those paths for the entire body.

#### 23.3.1 Key-Block Syntax

Verbatim from §19.2.1 (identical in Appendix B.9):

```ebnf
key_block_stmt ::= key_block_head key_path_list key_options? block_expr
key_block_head ::= decorated_identifier("%", "read")
                 | decorated_identifier("%", "write")
                 | decorated_identifier("%", "release") key_mode
                 | decorated_identifier("%", "speculative") "write"
key_mode       ::= "read" | "write"
key_path_list  ::= key_path_expr ("," key_path_expr)*
key_options    ::= "[" key_option ("," key_option)* ","? "]"
key_option     ::= "ordered"
```

The four heads are decorated identifiers: the `%` decorator is tokenized as
`Operator("%")`, followed by the named identifier. The decorator and head
identifier are adjacent in source; the `write` after `%speculative` is the
ordinary key-mode identifier.

The four heads:

- `%read` — acquires `Read`-mode keys. The body may read the keyed paths; writing them is `E-CON-0070`.
- `%write` — acquires `Write`-mode keys. The body may read and write the keyed paths.
- `%release read` / `%release write` — nested release (§23.5).
- `%speculative write` — speculative execution (§23.6).

`ParseStmt` dispatches to the key-block parser (**Parse-KeyBlock-Stmt**) whenever the current token is the operator `%` (§19.2.2). The `ordered` option is the only key option; it tunes same-base indexed-path checking (§23.4.5) and modifies acquisition over the whole path set, not any single path and not the head mode (§19.2.1). `#dynamic` is an *ordinary attribute* applied to the whole key-block statement; the `#` decorator belongs to the attribute mechanism of Chapter 9, not to `key_block_head`, `key_path_list`, or `key_options` (§19.2.1).

#### 23.3.2 Key Triples, Modes, and Mode Ordering

A key is a triple `⟨Path, Mode, Scope⟩` (§19.2.3, §19.2.4). The two modes are ordered:

```text
Read < Write
```

- A `Read` key permits read-only access; multiple `Read` keys to overlapping paths MAY coexist.
- A `Write` key permits read *and* write access and excludes every other key to an overlapping path.

`ModeSufficient(M_held, M_required) ⇔ M_required ≤ M_held`. A held `Write` key therefore covers a required `Read` (a writer may freely read), but a held `Read` does not cover a required `Write` (`E-CON-0005` when a write needs a key only `Read` is available).

The *required mode* of an access is determined by its syntactic context (§19.2.4). Read contexts (require `Read`):

| Syntactic Position | Context |
| --- | --- |
| Right-hand side of `let`/`var` initializer | Read |
| Right-hand side of assignment (`=`) | Read |
| Operand of arithmetic/logical operator | Read |
| Argument to `const` or `shared` parameter | Read |
| Condition or case-scrutinee expression (`if`, `if ... is`, `loop`) | Read |
| Receiver of method with `~` receiver | Read |

Write contexts (require `Write`):

| Syntactic Position | Context |
| --- | --- |
| Left-hand side of assignment (`=`) | Write |
| Left-hand side of compound assignment (`+=`, etc.) | Write |
| Receiver of method with `~!` receiver | Write |
| Receiver of method with `~%` receiver | Write |
| Argument to `unique` parameter | Write |

If an expression appears in multiple contexts, the *more restrictive* (i.e. `Write`) context applies (§19.2.4).

#### 23.3.3 Coverage and Implicit Acquisition

Coverage is prefix-based (§19.2.4):

```text
Covered(Q, M_Q, Γ_keys) ⇔ ∃ (P, M_P, S) ∈ Γ_keys : Prefix(P, Q) ∧ ModeSufficient(M_P, M_Q)
```

An access to `Q` at mode `M_Q` is covered if some held key has a *prefix* path `P` of `Q` and a sufficient mode. A held `Write` key on `account` covers a write to `account.balance`.

For an ordinary `shared` access `e`, a *valid key context* exists iff `KeyPath(e)` and `RequiredMode(e)` are both defined and no Chapter 19 scope/escape rule forbids the access (§19.2.4, **Valid Key Context**). If the access is already covered, it reuses the existing key context (**(K-Acquire-Covered)**); otherwise it establishes an implicit acquisition (**(K-Acquire-New)** / **(Lower-KeyAccess-Uncovered)**). Crucially: **being outside an explicit key block does not by itself make a `shared` access invalid.** An uncovered `shared` access acquires its own key for the duration the access needs it. A `shared` access with no derivable valid key context is `E-CON-0001`. A redundant acquisition that is already covered emits `W-CON-0002`.

Subexpressions are evaluated left-to-right, depth-first; key acquisition follows evaluation order (§19.2.4).

#### 23.3.4 Explicit Block Acquisition and Canonical Order

A key block acquires keys for all its paths in *canonical order* and in a fresh scope (§19.2.4):

```text
(K-Block-Acquire)
Γ ⊢ P_1, …, P_m : shared T_i    M = BlockMode(B)    (Q_1, …, Q_m) = CanonicalSort(P_1, …, P_m)    S = NewScope
─────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ'_keys = Γ_keys ∪ {(Q_i, M, S) : i ∈ 1..m}
```

`BlockMode` is `Read` for a `%read` block and `Write` for a `%write` block. Canonical sort (§19.3.3) is deterministic and is what makes multi-key acquisition deadlock-free: if every task acquires its keys in `CanonicalOrder`, no circular wait can occur (§19.6.5). Keys are released in *reverse* canonical order on body exit (§19.2.5, `ReleaseKeysSigma`).

A `%read` block that contains a write to one of its keyed paths is rejected (§19.2.4, **(K-Read-Block-No-Write)**, diagnostic `E-CON-0070`):

```text
(K-Read-Block-No-Write)
BlockMode(B) = Read    P ∈ KeyedPaths(B)    WriteOf(P) ∈ Body(B)
───────────────────────────────────────────────────────────────
Reject
```

A path named in a key block must be in scope (`E-CON-0031`) and must be `shared` (`E-CON-0032`).

#### 23.3.5 Scope-Bound Release and Escape Rules

Keys are released when their defining scope exits, *regardless of how it exits* — normal completion, `return`, `break`, `continue`, panic propagation, or task cancellation. Scope-exit key release occurs before drop actions for bindings in the same scope (§19.2.5).

Because validity is scope-bound, a key-dependent access **MUST NOT escape its defining scope** (§19.2.7). Concretely:

- An access whose validity depends on a key acquired in scope `S` is well-formed only when it is guaranteed to execute before `ScopeExit(S)`. A key escaping its defining scope is `E-CON-0004`.
- Key-dependent accesses MUST NOT escape through closures, deferred blocks, or suspension/resumption boundaries that may execute after the scope exits.
- **Acquiring a key inside a `defer` body is always ill-formed** (`E-CON-0006`), because the deferred body runs in the outer scope's exit phase, not at the lexical `defer` site.

Where a more specific Chapter 19 escape diagnostic applies, it takes precedence over `E-CON-0004` (§19.2.7).

#### 23.3.6 Closure Capture of `shared` Bindings

Key analysis interacts with closures through the closure dependency set (the `closure_deps` of Appendix B.2, owned by the closures chapter; §19.2.4 consumes that information). Let `SharedCaptures(C)` be the captured bindings with `shared` permission (§19.2.4):

- For a **local** closure, the body is analyzed as if executing in the defining scope: `KeyPath(C, x.p) = KeyPath(x.p)`.
- For an **escaping** closure, key paths are rooted at the runtime identities of captured references:

```text
(K-Closure-Escape-Keys)
C : |vec_T| → R [shared: deps]    Access(x.p, M) ∈ C.body
────────────────────────────────────────────────────────
KeyPath(C, x.p) = id(C.x).p
```

An escaping closure MUST NOT outlive any captured local `shared` binding (`E-CON-0086`, `P-Closure-Escape-Err`), and an escaping closure with a `shared` capture that lacks a dependency set is `E-CON-0085`. Any closure that captures `shared` data emits the advisory `W-CON-0009`. An implementation MAY conservatively coarsen `id(C.x).p` to a stable capture-rooted prefix, provided it soundly covers every runtime identity reachable through `C.x` and preserves observational equivalence (§19.2.4).

#### 23.3.7 Worked Example — A `%write` Block over Two Paths

```ultraviolet
/// A double-entry book of accounts.
public record Ledger {
    public credits: i64
    public debits: i64
}

/// Move `amount` from debits to credits atomically. A single `%write`
/// block holds Write keys to both fields for the whole transfer, so the
/// two updates cannot interleave with another task's transfer. The keys
/// are acquired in canonical order and released in reverse on exit.
public procedure settle(book: shared Ledger, amount: i64) {
    %write book.credits, book.debits {
        book.credits += amount
        book.debits -= amount
    }
}
```

### 23.4 Conflict Detection (§19.3)

Conflict detection decides whether two key paths overlap and whether their modes are compatible. It introduces no new surface syntax (§19.3.1).

#### 23.4.1 Prefix, Disjointness, and Overlap

From §19.3.3:

```text
Prefix(p_1 … p_m, q_1 … q_n) ⇔ m ≤ n ∧ ∀ i ∈ 1..m, p_i ≡_seg q_i
Disjoint(P, Q)                ⇔ ¬ Prefix(P, Q) ∧ ¬ Prefix(Q, P)
KeysOverlap(p_1, p_2)         ⇔ Prefix(p_1, p_2) ∨ Prefix(p_2, p_1) ∨ p_1 = p_2
```

Segment equivalence `p_i ≡_seg q_i` holds iff the names match and the indices are *provably equivalent*. Two index expressions are provably equivalent iff (§19.3.4): both are the same integer literal; both reference the same `const` binding; both reference the same generic const parameter; both reference the same variable binding in scope that is immutable (`let`) or has no intervening mutation between the two references; or both normalize to the same canonical form under constant folding. These are sufficient proof cases; an implementation may recognize any sound subset, and *failure to prove equivalence is treated as inequivalence*.

#### 23.4.2 Mode Compatibility and the Conflict Relation

From §19.3.3:

```text
Compatible(K_1, K_2) ⇔ Disjoint(P_1, P_2) ∨ (M_1 = Read ∧ M_2 = Read)

KeyModeCompatible(Read,  Read)  = true
KeyModeCompatible(Read,  Write) = false
KeyModeCompatible(Write, Read)  = false
KeyModeCompatible(Write, Write) = false

KeyConflict(⟨p_1, m_1, _⟩, ⟨p_2, m_2, _⟩) ⇔ KeysOverlap(p_1, p_2) ∧ ¬KeyModeCompatible(m_1, m_2)
```

Two keys conflict iff their paths overlap **and** their modes are incompatible. Disjoint paths never conflict (**(K-Disjoint-Safe)**: concurrent access to disjoint paths is statically safe), and two `Read` keys never conflict no matter how their paths overlap. Coverage is the static counterpart (**(K-Prefix-Coverage)**): a held `(P, M)` covers `Q` whenever `Prefix(P, Q)`.

#### 23.4.3 Canonical Ordering

The deterministic acquisition order is a lexicographic sort on path segments (§19.3.3):

```text
SegmentLess(s_1, s_2) ⇔
  (IsIdent(s_1) ∧ IsIdent(s_2) ∧ Utf8LexLess(Name(s_1), Name(s_2))) ∨
  (IsIndex(s_1) ∧ IsIndex(s_2) ∧ IndexValue(s_1) < IndexValue(s_2)) ∨
  (IsIdent(s_1) ∧ IsIndex(s_2))

CanonicalOrder(paths) = Sort(paths, KeyPathLess)
CanonicalSort(paths)  = Sort(paths, KeyPathLess)
```

Field segments sort before index segments at the same position; like segments sort by UTF-8 name or by index value. `CanonicalOrder` is the single deterministic acquisition and conflict-resolution order used by key blocks (§19.2.5), nested release (§19.4.5), and dynamic verification (§19.6.5). It is what guarantees no circular wait.

#### 23.4.4 Dynamic-Index Conflicts and Read-Then-Write

When two accesses in the *same statement* index the same array with dynamic indices that cannot be proven disjoint, the statement is rejected (§19.3.4):

```text
(K-Dynamic-Index-Conflict)
P_1 = a[e_1]   P_2 = a[e_2]   S = StatementOf(P_1)   S = StatementOf(P_2)
(Dynamic(e_1) ∨ Dynamic(e_2))   ¬ ProvablyDisjoint(e_1, e_2)   c = Code(K-Dynamic-Index-Conflict)
─────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ S ⇑ c
```

This is `E-CON-0010`. Two index expressions are provably disjoint iff (§19.3.4): both are statically resolvable to different values; a verification fact establishes `e_1 ≠ e_2`; a precondition asserts `e_1 ≠ e_2`; a refinement type constrains their relationship; they share a common base and differ by constant offsets; they are indexed by a `dispatch` iteration variable (automatically disjoint); or they come from loops with non-overlapping ranges. Failure to prove disjointness is treated as possible overlap.

**Read-then-write** within a single statement is prohibited unless a covering `Write` key is held (§19.3.4):

```text
ReadThenWrite(P, S) ⇔ ∃ e_r, e_w ∈ Subexpressions(S) : ReadsPath(e_r, P) ∧ WritesPath(e_w, P)

(K-Read-Write-Reject)
Γ ⊢ P : shared T    ReadThenWrite(P, S)    ¬ ∃ (Q, Write, S') ∈ Γ_keys : Prefix(Q, P)    c = Code(K-Read-Write-Reject)
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ S ⇑ c
```

This is `E-CON-0060`. This prohibition is diagnosed for assignment and compound-assignment statement surfaces that visibly separate a read of `P` from a write of `P`; other write forms remain governed by `RequiredMode`, `Covered`, and the ordinary key-compatibility rules (§19.3.4). With a covering `Write` key the same statement is permitted (**(K-RMW-Permitted)**), but the compiler still emits an advisory: `W-CON-0006` (an explicit read-then-write form was used while a compound-assignment form is available — `CompoundRewriteCandidate`, **(K-RMW-Explicit-Warn)**) or `W-CON-0004` (the read-then-write may contend if parallelized, **(K-RMW-Contention-Warn)**). The lesson is to hold a `Write` key (or use compound assignment such as `+=`) for any read-modify-write of `shared` data. The compound-rewrite operators are `CompoundRewriteOp(op) ⇔ op ∈ {+, -, *, /, %}`.

#### 23.4.5 The `ordered` Option

`ordered` requests same-base indexed-path checking. It is well-formed only when all keyed paths share an *ordered base* — the same root and same non-index segment shape (§19.3.4):

```text
NonIndexShape(P)         = [seg | seg ∈ PathSegments(P) ∧ ¬ IsIndex(seg)]
OrderedBase(P)           = ⟨Root(P), NonIndexShape(P)⟩
OrderedComparable(paths) ⇔ ∀ P, Q ∈ paths. OrderedBase(P) = OrderedBase(Q)

(K-Ordered-Ok)
options.ordered    OrderedComparable([KeyPath(p) | p ∈ paths])
─────────────────────────────────────────────────────────────
OrderedPathsOk(...)
```

If the keyed paths have different array bases, the `ordered` option is `E-CON-0014` (**(K-Ordered-Base-Err)**). If all indices are already statically comparable under `IndexValue`, `ordered` is redundant and emits `W-CON-0013` (**(K-Ordered-Redundant-Warn)**). The `ordered` commit also participates in the happens-before model (§23.8): the commit of an `ordered` block synchronizes-with the acquisition of the next block in canonical order (§19.7.5, clause 6).

#### 23.4.6 Worked Example — Disjoint Indices and a Covering Write

```ultraviolet
/// A histogram with a runtime-sized bucket slice.
public record Histogram {
    public buckets: [u64]
}

/// Two FIXED literal indices are provably disjoint, so the compiler proves
/// these accesses cannot conflict — no key block is needed for safety, and
/// each compound assignment is a permitted RMW under its own implicit key.
public procedure bumpEnds(h: shared Histogram) {
    h.buckets[0] += 1
    h.buckets[1] += 1
}

/// A read-modify-write of a single bucket. The `%write` block supplies the
/// covering Write key, so the `+=` is a permitted RMW rather than E-CON-0060.
public procedure accumulate(h: shared Histogram, slot: usize, weight: u64) {
    %write h.buckets[slot] {
        h.buckets[slot] += weight
    }
}
```

### 23.5 Nested Release (§19.4)

Sometimes a task holding a coarse `Write` key wants to *temporarily downgrade* — release the key, let other tasks interleave, then reacquire — without leaving the enclosing scope. That is what `%release` does. It introduces no new surface syntax beyond the `%release key_mode` head (§19.4.1).

#### 23.5.1 Semantics of `%release`

When entering `%release <target> path { body }`, execution performs five steps (§19.4.5):

1. Release the outer key held by the enclosing block.
2. Acquire the *target-mode* key for `path`.
3. Execute `body`.
4. Release the inner key.
5. Reacquire the *outer-mode* key for the remainder of the enclosing scope.

```text
(K-Release-Sequence)
Held(P, M_outer, S_outer)    %release M_inner P { B }
─────────────────────────────────────────────────────
Release(P, Γ_keys);
Acquire(P, M_inner, S_inner);
Eval(B);
Release(P, Γ_keys);
Acquire(P, M_outer, S_outer)
```

**Between steps 1 and 2, and between steps 4 and 5, other tasks MAY acquire conflicting keys to the same path** (§19.4.5). This is the entire point — and its hazard. Any binding derived from the `shared` data before the release may be *stale* after it. The compiler warns with `W-CON-0011` (access to a potentially stale binding after release) and `W-CON-0010` (the `%release` block permits interleaving). You can suppress the stale-after-release warning on a specific binding with the `#stale_ok` attribute (§19.4.4, `stale_ok_attribute`), but only do so when you have re-validated the value.

#### 23.5.2 The Mode-Change Rule

The target mode of a `%release` must *differ* from the outer mode (§19.4.4):

```text
(K-Nested-Same-Path)
Held(P, M_outer, Γ_keys)    key block for P has mode M_inner
───────────────────────────────────────────────────────────
if   M_inner = M_outer:                            Covered
otherwise if M_inner ≠ M_outer ∧ kind = Release:   Release-and-Reacquire
otherwise:                                         Reject
```

So if you hold `Write` and want a nested `Write` on the same path, you do *not* use `%release` — you are already covered. Using `%release` whose target mode equals the outer mode is `E-CON-0018` (**(K-Release-SameMode-Err)**). The legitimate uses are downgrade (`%release read` inside a `%write` block) and the symmetric upgrade. A nested *mode change attempted without* a `%release` head is `E-CON-0012`.

#### 23.5.3 Reentrancy Through `shared` Parameters

Passing a `shared` value as a procedure argument **does not itself acquire a key** (§19.4.4):

```text
Γ ⊢ f : (shared T) → R    Γ ⊢ v : shared T
───────────────────────────────────────────
call(f, v) ⇒ no key acquisition at call site
```

Instead, the compiler computes a `CalleeAccessSummary` for each procedure: the least set of `⟨parameter index, relative path, mode⟩` triples describing how the callee accesses its `shared` parameters, closed over directly resolved calls (§19.4.4, **(K-Reentrant)**). At a call site, if the actual argument's path lies under a *currently held* covering prefix and the instantiated callee access's required mode is covered, the callee access is `CalleeCovered` — no reacquisition occurs. This is how a caller holding a coarse key lets callees reuse it (reentrancy) instead of redundantly relocking.

If the summary cannot be computed — the callee is unresolved, bodyless, dynamically dispatched, or recursively unknown — the compiler MUST emit the unknown-callee-access warning `W-CON-0005` once per such call site whose `shared` actual argument path lies under a currently held prefix, and conservatively treats the call as accessing *every subpath of the argument in `Write` mode* (§19.4.4).

#### 23.5.4 Worked Example — Downgrade to Read During a Long Scan

```ultraviolet
/// A versioned cache. `entry_count` is the live element count callers track.
public record Cache {
    public version: u64
    public entries: [i64]
}

/// Hold a Write key on the cache, but downgrade to Read while running a
/// long disjoint scan so other writers can interleave. After the inner
/// block the Write key is reacquired for the final bump.
public procedure refreshAndBump(cache: shared Cache, entry_count: usize) {
    %write cache.version, cache.entries {
        %release read cache.entries {
            var i: usize = 0
            loop i < entry_count {
                // Read-only work; interleaving readers are permitted here.
                let _sample: i64 = cache.entries[i]
                i += 1
            }
        }

        // Write key on `cache.version` is held again from here.
        cache.version += 1
    }
}
```

### 23.6 Speculative Execution (§19.5)

Speculative execution runs a `Write` body *optimistically without holding a key*, snapshots the keyed paths it reads, and commits only if those paths are unchanged at commit time. On a validation failure it retries; after a bounded number of retries it falls back to ordinary keyed execution. This is Ultraviolet's transactional-memory primitive.

#### 23.6.1 Speculative-Block Syntax

Verbatim from §19.5.1:

```ebnf
speculative_block ::= decorated_identifier("%", "speculative") "write" key_path_list block_expr
```

The source spelling `%speculative` is the `%` decorator followed by the
identifier `speculative`; it must be followed by `write`. Any other token after
`%speculative` is `E-CON-0095` (**(K-Spec-Write-Required)**). It cannot be
combined with `%release`; no concrete key-block statement can have both
`SpeculativeWrite` and `Release` kind, so source that attempts to combine them
is rejected during parsing (`E-CON-0094`, **(K-Spec-No-Release)**).

#### 23.6.2 Body Restrictions

The body of a speculative block is tightly constrained (§19.5.4). Permitted: reads from keyed paths, writes to keyed paths, pure computation, and calls to `const`-receiver procedures on keyed data. Prohibited:

1. Writes to paths outside the keyed set (`E-CON-0091`; **(K-Spec-Pure-Body)** rejects `Writes(B) ⊄ CoveredPaths(P)`).
2. Nested key blocks (`E-CON-0090`; **(K-Spec-No-Nested-Key)**).
3. `wait` expressions (`E-CON-0092`; **(K-Spec-No-Wait)**).
4. Impure procedure calls — any `CallExpr`/`MethodCallExpr` (`IsCallLike`) that is not pure (`E-CON-0097`; **(K-Spec-No-Impure-Call)**).
5. `defer` statements (`E-CON-0093`; **(K-Spec-No-Defer)**).
6. Memory-ordering annotations and fence operations (`E-CON-0096`; **(K-Spec-No-Memory-Ordering)**).

These restrictions exist because a speculative body may execute multiple times and may be abandoned; only pure, retry-safe work belongs inside it.

#### 23.6.3 The Speculative State Machine

The dynamic semantics (§19.5.5) are an abstract state machine. On entry with `retries = 0`, the block snapshots its keyed paths into a read set `R`, evaluates the body collecting a write set `W` (writes are *intercepted*, not applied), then attempts to commit:

- **Commit success** (`Spec-Commit-Success`): if every `(p, v) ∈ R` still reads `v`, the writes `W` are applied atomically and the block produces its value.
- **Retry** (`Spec-Commit-Fail-Retry`): if some snapshotted path changed and `n < MAX_SPECULATIVE_RETRIES`, re-snapshot and re-execute.
- **Fallback** (`Spec-Commit-Fail-Fallback`): if validation keeps failing and `n ≥ MAX_SPECULATIVE_RETRIES`, fall back to a plain keyed `Write` execution that acquires the keys, runs the body, and releases.

`MAX_SPECULATIVE_RETRIES = 8`. If a *panic* occurs during speculative execution, the write set is discarded and the panic propagates (`Spec-Exec-Panic` → `SpecPanic`). The snapshot MUST be observationally equivalent to an atomic snapshot over the keyed set, and the commit MUST be atomic with respect to other key operations on overlapping paths. An implementation MAY conservatively realize a speculative block by *directly selecting the fallback path*, provided observable behavior matches some execution of the abstract semantics — so speculation is a permission to optimize, never a change in observable result.

The compiler emits performance advisories `W-CON-0020` (speculative block on a large struct — snapshotting may be costly) and `W-CON-0021` (body may be expensive to re-execute).

#### 23.6.4 Worked Example — A Speculative Counter Bump

```ultraviolet
/// A single shared counter.
public record Counter {
    public value: u64
}

/// Increment optimistically. If no other task touches `c.value` between the
/// snapshot and commit, the write applies in one shot; otherwise execution
/// retries up to MAX_SPECULATIVE_RETRIES, then falls back to a keyed Write.
public procedure bumpSpeculative(c: shared Counter) {
    %speculative write c.value {
        c.value = c.value + 1
    }
}
```

### 23.7 Dynamic Key Verification (§19.6)

The key system's goal is to *omit* runtime synchronization wherever it can prove safety statically, and to require runtime synchronization only where it cannot. Dynamic key verification governs that boundary. It introduces no new surface syntax; `#dynamic` is an ordinary attribute (Chapter 9, Appendix B.8 `dynamic_attribute ::= "#" "dynamic"`).

#### 23.7.1 Static Safety Conditions

`StaticallySafe(P)` is a conservative compile-time judgment with these sufficient proof shapes (§19.6.3):

| Condition | Description | Rule |
| --- | --- | --- |
| `No escape` | `shared` value never escapes to another task | K-SS-1 |
| `Disjoint paths` | Concurrent accesses target provably disjoint paths | K-SS-2 |
| `Sequential context` | No `parallel` block encloses the access | K-SS-3 |
| `Unique origin` | Value is `unique` at origin, temporarily viewed as `shared` | K-SS-4 |
| `Dispatch-indexed` | Access indexed by `dispatch` iteration variable | K-SS-5 |
| `Speculative-only` | All accesses occur within speculative blocks with fallback | K-SS-6 |

`StaticallySafe(P)` MUST be treated as `false` unless a *complete sound proof* exists for the concrete access. **Uncertainty is not success** (§19.6.3).

#### 23.7.2 Static vs. Dynamic Outcomes

```text
(K-Static-Safe)
Access(P, M)    StaticallySafe(P)
─────────────────────────────────
NoRuntimeSync(P)

(K-Static-Required)
¬ StaticallySafe(P)    ¬ InDynamicContext
──────────────────────────────────────────
Reject

(K-Dynamic-Permitted)
¬ StaticallySafe(P)    InDynamicContext
────────────────────────────────────────
EmitRuntimeSync(P)
```

When `NoRuntimeSync(P)` holds, an implementation MAY omit synchronization or MAY conservatively retain an equivalent (§19.6.4). When safety is *not* statically provable and the access is *not* in a `#dynamic` context, the program is rejected: `E-CON-0020` (key safety not statically provable outside `#dynamic`). Inside `#dynamic`, runtime synchronization is emitted: `I-CON-0011` (runtime synchronization emitted under `#dynamic`) or, if static safety is proven even there, `I-CON-0013` (static key safety proven under `#dynamic`).

So `#dynamic` is the explicit, auditable opt-in to runtime key checking — the deliberate boundary tool, not a way to silence the checker.

#### 23.7.3 Runtime Synchronization Guarantees

When runtime synchronization *is* required (§19.6.5): mutual exclusion is enforced by `KeyConflict`/`KeyModeCompatible` (§19.3.5); incompatible acquisitions block until release; keys are released on scope exit including panic; and implementations MUST guarantee eventual progress when conflicting holders eventually release. Within `#dynamic`, an ordering relation over incomparable dynamic indices must satisfy totality, antisymmetry, transitivity, cross-task consistency, and value-determinism. An implementation MAY conservatively *coarsen* a non-statically-safe dynamic indexed path to a static prefix that soundly covers every reachable runtime index, synchronizing on the coarsened path instead of per-index keys, iff that preserves mutual exclusion and observational equivalence. If all tasks acquire keys in `CanonicalOrder`, no circular wait can occur. Behavior under static-proof safety and under runtime synchronization MUST be observationally equivalent (§19.6.5).

#### 23.7.4 Worked Example — `#dynamic` for Data-Dependent Indices

```ultraviolet
/// A sparse grid whose write target is computed from data.
public record SparseGrid {
    public cells: [i64]
}

/// The write index is computed from data, so the compiler cannot statically
/// prove disjointness for concurrent callers. `#dynamic` opts this key block
/// into runtime synchronization (I-CON-0011) rather than rejecting it.
public procedure scatterAdd(grid: shared SparseGrid, target: usize, delta: i64) {
    #dynamic %write grid.cells[target] {
        grid.cells[target] += delta
    }
}
```

### 23.8 Memory Ordering (§19.7)

Memory-order attributes and fences tune the visibility ordering of keyed and `shared` data accesses. They never change *which* keys are acquired or released — they affect only data-access ordering (§19.7.4).

#### 23.8.1 Memory-Order Syntax

Verbatim from §19.7.1 (the `memory_order` set also appears in Appendix B.10):

```ebnf
memory_order_attribute ::= "#" memory_order
memory_order           ::= "relaxed" | "acquire" | "release" | "acqrel" | "seqcst"
fence_expr             ::= "fence" "(" fence_order ")"
fence_order            ::= "acquire" | "release" | "seqcst"
```

Memory-order attributes attach through the generic attribute parser (Chapter 9). `fence_expr` parses as an ordinary primary expression of type `()` (§19.7.4, **(T-Fence)**).

#### 23.8.2 Ordering Levels and Effective Ordering

Memory accesses default to sequentially consistent ordering. **Key acquisition uses acquire semantics; key release uses release semantics** (§19.7.4). The informative level summary (the normative model is the happens-before axiomatization of §19.7.5):

| Ordering | Guarantee |
| --- | --- |
| `relaxed` | Atomicity only; no ordering |
| `acquire` | Subsequent reads see prior writes |
| `release` | Prior writes are visible to acquire reads |
| `acqrel` | Both acquire and release |
| `seqcst` | Total global order |

A memory-order attribute MAY attach to (1) a *key-block statement*, establishing a default ordering for keyed/`shared` accesses in its body, or (2) an *attributed expression*, overriding any enclosing key-block default for that subtree. The effective ordering of an expression is (§19.7.3):

```text
EffectiveOrdering(e) =
  1. nearest enclosing expression-level memory-order attribute on e; else
  2. nearest enclosing key-block default memory-order attribute; else
  3. seqcst
```

Constraints (§19.7.4): a key block or attributed expression MUST carry at most one memory-order attribute; an expression-level memory-order attribute is well-formed only when the attributed expression contains keyed or `shared`-data access; memory-order attributes affect *only* data-access ordering and **MUST NOT alter key acquire or key release semantics**; and memory-order annotations MUST NOT appear inside speculative blocks (owned by §19.5.7, `E-CON-0096`).

#### 23.8.3 Fences

```text
(T-Fence)
O ∈ {acquire, release, seqcst}
──────────────────────────────
Γ ⊢ fence(O) : ()
```

Evaluating `fence(O)` emits the ordering event `Fence(O)` and produces `()`; it MUST NOT read or write program-visible storage, and it MUST NOT alter the held-key context (§19.7.4, §19.7.5). Fences MAY appear in runtime expression contexts. Note that `acqrel` and `relaxed` are *not* valid fence orders — `fence_order` admits only `acquire`, `release`, and `seqcst`.

#### 23.8.4 The Happens-Before Model and Key-Transfer Visibility

The normative model (§19.7.5) defines memory actions, a per-task sequenced-before order `sb`, a synchronizes-with relation `sw`, and `hb = (sb ∪ sw)⁺` (which MUST be irreflexive). The `sw` relation is the least relation containing six clauses; the most important for the key system is **key transfer** (clause 1): `KeyRelease(t_1, ks_1) sw KeyAcquire(t_2, ks_2)` whenever some key in `ks_1` overlaps some key in `ks_2` (`KeysOverlap`) and the release immediately precedes the acquire in that key's serialization order. Task creation, task completion, release/acquire accesses, fences, and ordered-commit each contribute the further `sw` edges of clauses 2–6.

The guarantee that makes the key system a complete substitute for mutexes is **Key-Transfer Visibility** (§19.7.5): if `A` is any access sequenced before `KeyRelease(t_1, ks)` in `t_1`, and `KeyAcquire(t_2, ks')` with overlapping keys synchronizes with that release, then `A hb B` for every `B` sequenced after the acquire in `t_2` — *regardless of any memory-order attributes on `A` or `B`*. Memory-order attributes weaken only same-location atomic visibility *outside* key transfer and the implementation's reordering latitude *inside* a held-key body; they MUST NOT weaken the clause-1 key-transfer `sw` edge.

Consequently (§19.7.5, `Data-Race-Freedom`, §8.4): programs whose safe fragment the compiler accepts have no data races — every `shared` access is key-covered and key transfer establishes happens-before. A data race can arise only through `unsafe`, raw pointers, or FFI, which implies `OutsideConformance`.

#### 23.8.5 Worked Example — A Release Fence Around a Handoff

```ultraviolet
/// A single-slot mailbox: a payload plus a readiness flag.
public record Mailbox {
    public ready: bool
    public payload: i64
}

/// Publish a payload, then set the readiness flag. The release fence orders
/// the payload write before the readiness write; a task that later acquires
/// an overlapping key and performs an acquire fence observes them in order.
public procedure publish(box: shared Mailbox, message: i64) {
    %write box.payload, box.ready {
        box.payload = message
        let _ordered: () = fence(release)
        box.ready = true
    }
}
```

### 23.9 Key Propagation and the Static Replacement of Mutexes

Three mechanisms compose to let keys flow through a program without per-access locking ceremony:

1. **`shared` parameters carry the obligation, not the key.** Passing a `shared` value to a procedure acquires nothing (§19.4.4). The callee's accesses form their own key paths rooted at the parameter, and `CalleeAccessSummary` lets a caller's held key *cover* those accesses by prefix. A coarse key held high in the call tree is reused all the way down.
2. **Coverage is prefix-based.** A single `%write account` block at the top of an operation covers every `account.<field>` access inside it and inside everything it calls (subject to summary computation), so a multi-procedure transaction needs exactly one acquisition.
3. **Static safety elides synchronization entirely.** Where `StaticallySafe(P)` holds — disjoint `dispatch`-indexed accesses, `unique`-origin data temporarily viewed as `shared`, sequential context — `NoRuntimeSync(P)` permits the implementation to drop synchronization with no observable difference (§19.6.4).

A runtime mutex can do none of these: it cannot prove coverage statically, cannot elide itself, and cannot be checked against the data it nominally guards. The key system replaces it with a compile-time proof that is exactly as strong as the program needs and no stronger.

### 23.10 Capstone Worked Example — A Guarded Shared Counter

This example ties together implicit acquisition, an explicit `%write` block for an atomic read-modify-write, key propagation through a `shared` parameter, and a `%read` block for a consistent multi-field read. (The `parallel`/`dispatch` task constructs that would exercise it concurrently are specified in *Structured Parallelism*; here the focus is the key discipline.)

```ultraviolet
//! A bounded, guarded counter demonstrating the key system end to end.

/// A counter paired with the largest value it has ever held.
public record GuardedCounter {
    public value: u64
    public peak: u64
}

/// Read the current value. The `return` expression is a `Read` context, so
/// it forms a `Read` key path `counter.value`; an uncovered read acquires
/// its own key implicitly for the duration of the access.
public procedure currentValue(counter: shared GuardedCounter) -> u64 {
    return counter.value
}

/// Atomically increment and update the peak. A single `%write` block holds
/// Write keys to both fields, so the read of `value`, the write of `value`,
/// and the conditional update of `peak` form one indivisible transaction.
/// No other task can observe `value` updated but `peak` stale.
public procedure increment(counter: shared GuardedCounter) {
    %write counter.value, counter.peak {
        counter.value += 1
        if counter.value > counter.peak {
            counter.peak = counter.value
        }
    }
}

/// Read both fields consistently. A `%read` block over both paths guarantees
/// the pair is observed from a single coherent snapshot; writing either path
/// inside this block would be rejected as E-CON-0070.
public procedure snapshot(counter: shared GuardedCounter) -> (u64, u64) {
    %read counter.value, counter.peak {
        let current: u64 = counter.value
        let high: u64 = counter.peak
        return (current, high)
    }
}

/// Increment `n` times. The caller holds nothing special; each call to
/// `increment` forms and releases its own Write keys. Passing `counter` as a
/// `shared` argument does not itself acquire a key (§19.4.4).
public procedure incrementMany(counter: shared GuardedCounter, n: u64) {
    var i: u64 = 0
    loop i < n {
        increment(counter)
        i += 1
    }
}
```

What the compiler guarantees about this code: every access to `counter.value` and `counter.peak` is key-covered; the `%write` block in `increment` acquires its two keys in canonical order and releases them in reverse on every exit path; concurrent `increment` calls from different tasks cannot hold overlapping `Write` keys simultaneously (`KeyConflict`); key release in one task synchronizes-with key acquisition in the next (Key-Transfer Visibility), so the incremented `value` and updated `peak` are visible to the next holder; and the whole program — absent `unsafe`/FFI — is data-race-free by construction.

### Idioms & Best Practices

- **Prefer implicit acquisition for single touches; reach for a key block only when you need a *shared key context* across multiple accesses.** A lone `counter.value += 1` does not need a `%write` block to be safe in a sequential context, but a multi-step read-modify-write that must be atomic does. Use the block to make the *transaction boundary* explicit.
- **Hold the coarsest key that the operation actually needs, and no coarser.** Coverage is prefix-based, so one `%write account { ... }` covers every field access inside the operation and its callees. Keep authority narrow: do not key the whole `World` when the operation only touches `world.chunks[i]`.
- **Use `#` coarsening to silence `W-CON-0001` in tight loops** when per-iteration fine-grained acquisition is the bottleneck and a materially coarser legal boundary exists — but verify the coarser key does not serialize work that was meant to run concurrently.
- **Acquire multiple keys via a single key block, never by nesting independent blocks in ad hoc order.** `CanonicalSort` over a single block's path list is what guarantees deadlock freedom; hand-ordered nested blocks defeat it.
- **Use compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) for read-modify-write of `shared` data** under a covering `Write` key. It is the form the compiler recognizes (`CompoundRewriteCandidate`) and steers you toward via `W-CON-0006`.
- **Treat `#dynamic` and `%speculative write` as deliberate boundary tools.** `#dynamic` is the auditable opt-in to runtime key checking when static disjointness is genuinely unprovable; `%speculative write` is for retry-safe, side-effect-free optimistic updates. Neither is a way to bypass correct static conformance.
- **Re-validate any value read before a `%release` boundary before trusting it afterward;** apply `#stale_ok` only to a binding you have re-derived or re-read, never to silence a real staleness bug.
- **Encode invariants in types and contracts, not in lock discipline.** A `modal` resource state or a refinement type that proves index disjointness lets the compiler discharge `StaticallySafe(P)` and elide synchronization, which a runtime mutex never could.
- **Document the key path each public `shared`-taking procedure expects to cover** in its `///` doc comment, since `CalleeAccessSummary` is what lets callers reuse a held key — the contract is part of the API surface.

### Pitfalls & Diagnostics

- **`E-CON-0001` — `shared` access outside a valid key context.** The access has no derivable `KeyPath`/`RequiredMode`, or a scope/escape rule forbids it. Fix the root cause; do not paper over it with `#dynamic`.
- **`E-CON-0002` / `E-CON-0003` / `E-CON-0030` / `E-CON-0033` — `#` misuse.** `#` on a non-`shared` path, more than one `#` in a path expression, `#` immediately before a method name, or `#` on a field of a non-record type. A path may carry at most one marker, only on `shared` data.
- **`E-CON-0034` — key-path root cannot be derived.** The compiler could not derive a key-path root for a `shared` access (for example, an access whose root is lost across a boundary). Restructure so the access has a derivable root.
- **`E-CON-0004` / `E-CON-0006` — key escape.** A key escaped its defining scope, or a key was acquired inside a `defer` body (always ill-formed, since the deferred body runs in the outer exit phase). Keep key-dependent work strictly inside the acquiring scope.
- **`E-CON-0005` — write needs a key only `Read` provides.** You wrote a `shared` path under a `Read` key. Promote the enclosing block to `%write`, or restructure so the write happens where a `Write` key is held.
- **`E-CON-0010` — dynamic-index conflict in one statement.** Two dynamic indices into the same array in a single statement that cannot be proven disjoint. Prove disjointness (literals, a precondition `e_1 ≠ e_2`, a refinement, `dispatch`-indexing, or non-overlapping loop ranges), or split the statement and key the accesses separately.
- **`E-CON-0012` / `E-CON-0018` — `%release` mode errors.** A nested mode change without a `%release` head is `E-CON-0012`; a `%release` whose target mode equals the outer mode is `E-CON-0018` (you are already covered). Use `%release read` to downgrade from `Write`; do not use `%release` to "re-take" the same mode.
- **`E-CON-0014` — `ordered` on different array bases.** All paths in an `ordered` block must share `OrderedBase`. `W-CON-0013` warns when `ordered` is redundant because the indices are already statically comparable.
- **`E-CON-0031` / `E-CON-0032` — key-block path problems.** A path named in a key block is not in scope (`E-CON-0031`) or is not `shared` (`E-CON-0032`). Name an in-scope `shared` place.
- **`E-CON-0060` — read-then-write without a covering `Write` key.** A statement reads and writes the same `shared` path with no covering `Write` key. Wrap it in `%write` (then prefer compound assignment) — heed advisories `W-CON-0004` (contention) and `W-CON-0006` (compound form available).
- **`E-CON-0070` — write inside `%read`.** A `%read` block may only read its keyed paths. Use `%write` if the body must mutate them.
- **`E-CON-0083` — `shared $Class` with non-`const` dynamic methods.** A `shared` dynamic class object requires every vtable-eligible method to have a `~` receiver. Make the offending methods `const`-receiver or drop the `shared` qualification.
- **`E-CON-0085` / `E-CON-0086` — escaping closure with `shared` capture.** Missing dependency set (`E-CON-0085`), or the closure outlives a captured local `shared` binding (`E-CON-0086`, `P-Closure-Escape-Err`). Supply the `[shared: { ... }]` dependency set and ensure the closure does not outlive its captures; `W-CON-0009` flags any `shared`-capturing closure for review.
- **`E-CON-0090`–`E-CON-0097` — speculative-block violations.** Nested key block (`E-CON-0090`), write outside the keyed set (`E-CON-0091`), `wait` (`E-CON-0092`), `defer` (`E-CON-0093`), combining `%speculative`/`%release` (`E-CON-0094`), `%speculative` not followed by `write` (`E-CON-0095`), a memory-order/fence operation (`E-CON-0096`), or an impure call (`E-CON-0097`). Keep speculative bodies pure and retry-safe; move impure or blocking work outside.
- **`E-CON-0020` — key safety not statically provable outside `#dynamic`.** The compiler could not construct a sound `StaticallySafe(P)` proof and you are not in a `#dynamic` context. Either supply the proof (disjointness facts, contracts, `dispatch`-indexing) or, if the indices truly are data-dependent, opt into `#dynamic` (`I-CON-0011`/`I-CON-0013`). Remember: uncertainty is not safety.
- **`W-CON-0010` / `W-CON-0011` — interleaving and stale reads across `%release`.** Other tasks may take conflicting keys during the release window; bindings derived before the release may be stale. Re-read or re-validate after the inner block; use `#stale_ok` only on values you have re-established.
- **`W-CON-0005` — unknown callee access pattern.** A call whose `shared` argument lies under a held prefix could not be summarized (unresolved, bodyless, dynamically dispatched, or recursive). The analysis conservatively assumes full `Write` access to the argument. Resolve or annotate the callee so its access summary can be computed.
- **`W-CON-0001` / `W-CON-0002` / `W-CON-0003` — granularity hints.** Fine-grained keys in a tight loop, a redundant acquisition already covered by a held key, or a `#` marker that merely restates a type boundary. Coarsen, remove the redundant block, or drop the redundant marker as appropriate.
- **`W-CON-0020` / `W-CON-0021` — speculative cost hints.** A speculative block over a large struct may be costly to snapshot (`W-CON-0020`), or its body may be expensive to re-execute on retry (`W-CON-0021`). Narrow the keyed set or move expensive work outside the speculative region.
