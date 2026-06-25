## 25. Asynchronous Operations

Ultraviolet models every asynchronous computation as a value of one built-in modal type, `Async`. There is no separate "future trait", no hidden executor coupling, and no implicit polling protocol baked into the type system. An async computation is a *modal state machine* — a value that is `@Suspended`, `@Completed`, or `@Failed`, and that you advance explicitly (or via a composition form that advances it for you). The suspension forms (`wait`, `yield`, `yield from`) drive that machine, the composition forms (`sync`, `race`, `all`, `loop … in`, and the `~>map`/`~>filter`/`~>take`/`~>fold`/`~>chain` combinators) combine machines, and the key system integrates with suspension so that held access rights are released and reacquired correctly across a yield.

This chapter is the definitive reference for §21 of the specification. It covers the `Async<…>` type and its aliases (`Sequence`, `Future`, `Stream`, `Pipe`, `Exchange`), every suspension and composition form, the compiled state-machine model and its zero-cost suspension guarantee, and the async/key integration rules.

Related material: modal types (the `@State` model that `Async` is built on), error handling (the `?` propagation operator that settles a failing async into `@Failed`), keys and concurrency (`spawn`, key acquisition blocks, key release/reacquire), and capabilities (the `Reactor` and `System` capabilities required by async runtimes and timing).

Two cross-cutting language facts are assumed throughout and matter for every example:

- A reassignable local binding uses `var`, never `let mut`. `let` introduces an immutable binding; `var` introduces a mutable one. The token `mut` is a raw-pointer qualifier (`imm`/`mut`), not a binding modifier.
- Procedure parameters are immutable bindings (introduced as `let`), and the only parameter mode is `move`. A procedure cannot reassign a parameter; rebind it to a local `var` first.

### 25.1 The `Async` Type

#### 25.1.1 The four type parameters

`Async` is a built-in `modal` declaration with four type parameters, in order:

```text
Async<Out, In, Result, E>
```

- `Out` — the **output** type produced at each suspension (the value carried by `@Suspended { output }`). No default.
- `In` — the **input** type accepted on resumption (the value `~>resume(input)` injects). Default `()`.
- `Result` — the **result** type produced on successful completion (the value carried by `@Completed { value }`). Default `()`.
- `E` — the **error** type produced on failure (the value carried by `@Failed { error }`). Default `!`.

The reserved parameter list, verbatim from the built-in declaration:

```text
AsyncParams = [
  ⟨`Out`, [], ⊥, ⊥⟩,
  ⟨`In`, [], TypePrim("()"), ⊥⟩,
  ⟨`Result`, [], TypePrim("()"), ⊥⟩,
  ⟨`E`, [], TypePrim("!"), ⊥⟩
]
```

`Out` has no default, so at minimum `Async<Out>` must be written; `Async<T>` denotes `Async<T, (), (), !>` after defaults are applied.

#### 25.1.2 The three states

`Async` has exactly three modal states, each carrying one field:

```text
States(`Async`) = { `@Suspended`, `@Completed`, `@Failed` }

Payload(`Async`, `@Suspended`) = [⟨`output`, Out⟩]
Payload(`Async`, `@Completed`) = [⟨`value`,  Result⟩]
Payload(`Async`, `@Failed`)    = [⟨`error`,  E⟩]
```

These three states are reserved and are written with ordinary modal-state type syntax: `Async@Suspended`, `Async@Completed`, `Async@Failed`. Each field is `public`: you read a suspended async's emitted value as `a.output`, a completed async's value as `a.value`, and a failed async's error as `a.error`.

The `@Suspended` state additionally provides a `resume` method. Its declared shape (from the built-in declaration) is a `unique` receiver, one parameter `input` of type `In`, and a union return type spanning all three states:

```text
resume(unique self, input: In)
  -> (unique Async@Suspended) | (unique Async@Completed) | (unique Async@Failed)
```

Manual `resume` consumes a `unique` suspended receiver and returns a `unique` async state value in one of the three states. When `E = !`, the `@Failed` arm is uninhabited and lowering omits it from the concrete resume-result tag space (see §25.4).

#### 25.1.3 Syntax and parsing

This section introduces **no new concrete type grammar**. `Async`, `Sequence`, `Future`, `Stream`, `Pipe`, and `Exchange` use ordinary path and type-application parsing; `Async@Suspended`, `Async@Completed`, and `Async@Failed` use ordinary modal-state type parsing.

The bare path `Async` (with no type arguments) *may* parse, but is rejected by the static rule **(WF-Async-Path-Err)**: an unapplied `Async` is ill-formed. You must always apply it.

#### 25.1.4 Built-in aliases

Five convenience aliases are reserved. Each normalizes to an `Async<…>` application:

| Alias | Normalizes to | Meaning |
| --- | --- | --- |
| `Sequence<T>` | `Async<T, (), (), !>` | An infallible producer of `T` values; no input, no meaningful result. |
| `Future<T, E>` (`E` default `!`) | `Async<(), (), T, E>` | A single eventual `T`, or failure `E`; no intermediate output. |
| `Stream<T, E>` | `Async<T, (), (), E>` | A fallible producer of `T` values. |
| `Pipe<In, Out>` | `Async<Out, In, (), !>` | A bidirectional channel: yields `Out`, resumes with `In`. |
| `Exchange<T>` | `Async<T, T, T, !>` | Output, input, and result all `T`. |

Verbatim alias declarations:

```text
SequenceDecl = TypeAliasDecl(⊥, `public`, `Sequence`, [⟨`T`, [], ⊥, ⊥⟩], ⊥, TypeApply(["Async"], [TypePath(["T"]), TypePrim("()"), TypePrim("()"), TypePrim("!")]), ⊥, ⊥)
FutureDecl   = TypeAliasDecl(⊥, `public`, `Future`,   [⟨`T`, [], ⊥, ⊥⟩, ⟨`E`, [], TypePrim("!"), ⊥⟩], ⊥, TypeApply(["Async"], [TypePrim("()"), TypePrim("()"), TypePath(["T"]), TypePath(["E"])]), ⊥, ⊥)
StreamDecl   = TypeAliasDecl(⊥, `public`, `Stream`,   [⟨`T`, [], ⊥, ⊥⟩, ⟨`E`, [], ⊥, ⊥⟩], ⊥, TypeApply(["Async"], [TypePath(["T"]), TypePrim("()"), TypePrim("()"), TypePath(["E"])]), ⊥, ⊥)
PipeDecl     = TypeAliasDecl(⊥, `public`, `Pipe`,     [⟨`In`, [], ⊥, ⊥⟩, ⟨`Out`, [], ⊥, ⊥⟩], ⊥, TypeApply(["Async"], [TypePath(["Out"]), TypePath(["In"]), TypePrim("()"), TypePrim("!")]), ⊥, ⊥)
ExchangeDecl = TypeAliasDecl(⊥, `public`, `Exchange`, [⟨`T`, [], ⊥, ⊥⟩], ⊥, TypeApply(["Async"], [TypePath(["T"]), TypePath(["T"]), TypePath(["T"]), TypePrim("!")]), ⊥, ⊥)
```

Note carefully: in `Pipe<In, Out>`, the *first* alias parameter is `In` and the *second* is `Out`, but the normalized `Async` body places `Out` first and `In` second (`Async<Out, In, (), !>`). Read alias parameter order from the alias declaration, not from the underlying `Async` parameter positions. So `Pipe<Command, Ack>` normalizes to `Async<Ack, Command, (), !>`.

#### 25.1.5 `AsyncSig` — the normalization that drives every async rule

Every static rule in this chapter is phrased over the helper `AsyncSig`, which normalizes a type through its aliases, applies defaults, and extracts the four-tuple `⟨Out, In, Result, E⟩`:

```text
AsyncSig(T) = ⟨Out, In, Result, E⟩ ⇔
  AliasNorm(T) = TypeApply(["Async"], args) ∧
  DefaultArgs(AsyncParams, args) = [Out, In, Result, E]

AsyncSig(T) = ⊥    otherwise
```

A procedure is an **async procedure** exactly when its declared return type `R` satisfies `AsyncSig(R) ≠ ⊥`. For an async procedure, the type the body actually `return`s is the `Result` component, captured by:

```text
BodyReturnType(R) =
  { Result    if AsyncSig(R) = ⟨Out, In, Result, E⟩
    R         otherwise }
```

So inside `procedure foo() -> Sequence<i32>`, a `return e` statement is checked against `()` (the `Result` of `Sequence`), **not** against `Sequence<i32>`.

#### 25.1.6 Subtyping and variance

`Async` subtyping is defined once by **(Sub-Async)**. It is covariant in `Out`, `Result`, and `E`, and **contravariant in `In`**:

```text
(Sub-Async)
AsyncSig(T) = ⟨Out_1, In_1, Result_1, E_1⟩    AsyncSig(U) = ⟨Out_2, In_2, Result_2, E_2⟩
Γ ⊢ Out_1 <: Out_2    Γ ⊢ In_2 <: In_1    Γ ⊢ Result_1 <: Result_2    Γ ⊢ E_1 <: E_2
────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T <: U
```

#### 25.1.7 Well-formedness and diagnostics

`Async<args>` is well-formed when its (default-filled) arguments are each well-formed:

```text
(WF-Async)
T = TypeApply(["Async"], args)    DefaultArgs(AsyncParams, args) = args'    ∀ i, Γ ⊢ args'_i wf
────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ T wf
```

The error cases:

| Rule | Condition |
| --- | --- |
| **(WF-Async-ArgCount-Err)** | Too many/too few arguments (`DefaultArgs` fails). |
| **(WF-Async-Arg-WF-Err)** | Some argument is not well-formed. |
| **(WF-Async-Path-Err)** | Bare unapplied `Async` was written. |

When `E = !`, values of `Async@Failed` are uninhabited; the state exists in the model but no `@Failed` value can be constructed, and lowering omits the failed-state variant entirely.

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0201` | Error | `Async` type parameter is not well-formed |

#### 25.1.8 Worked example — declaring async procedures

```ultraviolet
/// A finite, infallible producer of the first `count` natural numbers.
/// `Sequence<i32>` is `Async<i32, (), (), !>`: it yields `i32`, takes no
/// resume input, completes with `()`, and cannot fail.
public procedure naturals(count: usize) -> Sequence<i32> {
    var current: i32 = 0
    loop current < count as i32 {
        yield current
        current = current + 1
    }
    return ()
}

/// A single eventual value that may fail. `Future<Config, ConfigError>`
/// is `Async<(), (), Config, ConfigError>`: no output, no input, result
/// `Config`, error `ConfigError`.
public procedure loadConfig(reader: const ConfigReader) -> Future<Config, ConfigError> {
    let raw: RawConfig = reader~>read()?
    let config: Config = parseConfig(raw)?
    return config
}
```

In `naturals`, `current` is a `var` because it is reassigned; `yield current` produces `Out = i32`; `return ()` settles `@Completed { value = () }`. In `loadConfig`, each `?` propagates a `ConfigError` into `@Failed { error }`, and `return config` settles `@Completed { value = config }`.

### 25.2 Suspension Forms

Three primary expressions suspend or await: `wait`, `yield`, and `yield from`. Their grammar:

```ebnf
wait_expr       ::= "wait" expression
yield_expr      ::= "yield" "release"? expression
yield_from_expr ::= "yield" "release"? "from" expression
```

All three are primary expressions. The optional `release` modifier (parsed as a leading contextual identifier `release` after `yield`) governs key handling at the suspension point (§25.5). `yield from` is distinguished from `yield` purely by the presence of the `from` keyword after the optional `release`.

#### 25.2.1 `wait` — awaiting a handle

`wait` does **not** operate on `Async` values. It awaits a concurrency *handle* — either a `Spawned<T>` handle (from `spawn`) or a `Tracked<T, E>` handle:

```text
(T-Wait)
Γ; R; L ⊢ h : TypeApply(["Spawned"], [T])
──────────────────────────────────────────
Γ; R; L ⊢ `wait` h : T

(T-Wait-Future)
Γ; R; L ⊢ h : TypeApply(["Tracked"], [T, E])
──────────────────────────────────────────
Γ; R; L ⊢ `wait` h : T | E
```

Waiting on a `Spawned<T>` yields `T`; waiting on a `Tracked<T, E>` yields the union `T | E`. Any other operand type is rejected by **(Wait-Handle-Err)** (`E-CON-0132`).

Dynamically, `wait` evaluates the handle, returns the settled value if the handle is ready, and otherwise blocks the current task until the handle settles. A `Spawned<T>` handle that settles by panic is **not** observed by `wait` directly; that failure is consumed by the enclosing `parallel` panic propagation. `wait` is subject to a key restriction: it is ill-formed while any key is held (§25.5), and it has no `release` form.

```ultraviolet
/// Spawns two independent computations and waits on their handles.
/// `wait worker_a` produces the spawned block's result type directly.
public procedure renderBoth(scene: const Scene) -> FrameImage {
    let worker_a: Spawned<Layer> = spawn { renderBackground(scene) }
    let worker_b: Spawned<Layer> = spawn { renderForeground(scene) }

    let background: Layer = wait worker_a
    let foreground: Layer = wait worker_b
    return compose(background, foreground)
}
```

#### 25.2.2 `yield` — suspending the current async computation

`yield e` is legal only inside an async procedure (one whose return type has an `AsyncSig`). It produces a value of type `In` (the resume input) and requires `e` to be a subtype of `Out`:

```text
(T-Yield)
AsyncSig(R) = ⟨Out, In, Result, E⟩    Γ; R; L ⊢ e : T    Γ ⊢ T <: Out
────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ YieldExpr(release_opt, e) : In
```

- Outside an async procedure (`AsyncSig(R) = ⊥`): **(Yield-NotAsync-Err)**, `E-CON-0210`.
- Operand not a subtype of `Out`: **(Yield-Out-Err)**, `E-CON-0211`.

Dynamically, `yield e`:
1. evaluates `e` to `v`;
2. if `release` is present, releases all held keys and records the released key set;
3. transitions to `@Suspended { output = v }`;
4. on resumption with input `i`, reacquires any recorded keys in canonical order, binds `i` as the value of the suspended `yield` expression, and continues.

Because `yield` *evaluates to* the resume input, a `Pipe<In, Out>` consumer reads each resumed input as the value of the `yield` expression:

```ultraviolet
/// `Pipe<Command, Ack>` is `Async<Ack, Command, (), !>`: each `yield ack`
/// produces an `Ack`, and the `yield` expression evaluates to the next
/// `Command` supplied by the resumer.
public procedure commandPipe() -> Pipe<Command, Ack> {
    var pending: usize = 0
    loop {
        let command: Command = yield Ack::Pending { queued: pending }
        pending = pending + 1
        applyCommand(command)
    }
}
```

#### 25.2.3 `yield from` — delegating to another async value

`yield from e` delegates suspension and completion to an inner async value `e`. It requires the inner `Out` and `In` to be *equivalent* to the enclosing async's `Out`/`In`, and the inner error type to be a subtype of the enclosing error type. It evaluates to the inner async's `Result`:

```text
(T-Yield-From)
AsyncSig(R) = ⟨Out, In, Result, E_1⟩    Γ; R; L ⊢ e : T_e
AsyncSig(T_e) = ⟨Out_e, In_e, Result_e, E_2⟩
Γ ⊢ Out_e ≡ Out    Γ ⊢ In_e ≡ In    Γ ⊢ E_2 <: E_1
───────────────────────────────────────────────────────
Γ; R; L ⊢ YieldFromExpr(release_opt, e) : Result_e
```

The error cases, with their diagnostics:

| Rule | Diagnostic | Condition |
| --- | --- | --- |
| **(YieldFrom-NotAsync-Err)** | `E-CON-0220` | Used outside an async procedure |
| **(YieldFrom-Out-Err)** | `E-CON-0221` | Operand is not async, or `Out_e ≢ Out` |
| **(YieldFrom-In-Err)** | `E-CON-0222` | `In_e ≢ In` |
| **(YieldFrom-ErrType-Err)** | `E-CON-0225` | `E_2` is not a subtype of `E_1` |

Dynamically, `yield from`:
1. evaluates the source async value;
2. if it is `@Suspended`, re-emits its output through the enclosing `yield` (preserving `release_opt`), threading resume input back into the inner machine;
3. if it is `@Completed`, produces the completed value as the result of the `yield from` expression;
4. if it is `@Failed`, propagates the failure out of the enclosing async procedure.

```ultraviolet
/// Re-emits every value of an inner sequence, then continues.
/// `Out`, `In`, and the error type must match, so both procedures are
/// `Sequence<i32>` (Out = i32, In = (), error = !).
public procedure chained() -> Sequence<i32> {
    yield from naturals(3)
    yield 100
    return ()
}
```

#### 25.2.4 Key restrictions

`wait`, `yield`, and `yield from` are all subject to the key-held restrictions of §25.5: holding a key across a bare suspension is rejected. Use `yield release` / `yield release from` to suspend while keys are held; for `wait`, release the key first.

### 25.3 Composition Forms

Composition forms combine async computations. There are five surface forms — `loop … in`, `sync`, `race`, `all`, and the method-call combinators — plus manual stepping.

```ebnf
sync_expr    ::= "sync" expression
race_expr    ::= "race" "{" race_arm ("," race_arm)* ","? "}"
race_arm     ::= expression "->" "|" pattern "|" race_handler
race_handler ::= expression | "yield" expression
all_expr     ::= "all" "{" expression ("," expression)* ","? "}"
```

`sync`, `race`, and `all` are primary expressions. The combinators (`map`, `filter`, `take`, `fold`, `chain`) and `until` use the ordinary `~>` method-call operator; async iteration uses the ordinary `loop pattern in expression { … }` loop. No dedicated AST nodes are introduced for the combinators or `until`.

#### 25.3.1 Async iteration — `loop pat in async`

A `loop pat in iter { body }` where `iter` is an async value drives that async to completion, binding each suspended `output` to `pat`:

```text
(T-Loop-Iter-Async)
AsyncSig(R) = ⟨Out_r, In_r, Result_r, E_r⟩    Γ; R; L ⊢ iter : T_iter
AsyncSig(T_iter) = ⟨Out_i, In_i, Result_i, E_i⟩    In_i = ()    Γ ⊢ E_i <: E_r
… (ty_opt = ⊥ ⇒ T_p = Out_i) ; (ty_opt = T_a ⇒ Out_i <: T_a ∧ T_p = T_a) ; pat binds T_p …
```

Two hard requirements:
- The loop must appear **inside an async procedure** (`AsyncSig(R) ≠ ⊥`), because errors of the iterated async are propagated out of the enclosing async.
- The iterated async must have `In = ()`. An async with a non-`()` input cannot be driven by a `loop`; you must step it manually. Violations are **(Loop-Async-Err)**, `E-CON-0240`.

The error type of the iterated async must be a subtype of the enclosing async's error type, so failures propagate cleanly. Optionally, `loop pat: T in iter` annotates the binding type, requiring `Out_i <: T`.

Dynamically: bind `@Suspended { output }` to `pat`, run `body`, resume with `()`; stop on `@Completed`; propagate the error on `@Failed`.

```ultraviolet
/// Drives an inner `Sequence<i32>` and re-emits each squared value.
/// The loop is inside an async procedure and the iterated async has In = ().
public procedure squares(count: usize) -> Sequence<i32> {
    loop n in naturals(count) {
        yield n * n
    }
    return ()
}
```

#### 25.3.2 Manual stepping

When an async value has `In ≠ ()` (so a `loop` cannot drive it), advance it by hand: inspect its modal state with `if … is @Suspended`, read `output`, and call `~>resume(input)` on the `@Suspended` state. `resume` consumes the `unique` receiver and returns the next `unique` state value, so the stepped binding must be a `var` you reassign.

Because parameters are immutable, a procedure that steps an async it received as a parameter must first move the parameter into a `var` local:

```ultraviolet
/// Manually steps a `Pipe<Command, Ack>` because In = Command ≠ ().
/// The parameter is immutable, so it is moved into a `var` before stepping.
public procedure runPipe(pipe: unique Pipe<Command, Ack>) -> () {
    var state: unique Pipe<Command, Ack> = move pipe
    loop {
        if state is @Suspended {
            let next: Command = decideNextCommand(state.output)
            state = state~>resume(next)
        } else {
            return ()
        }
    }
}
```

#### 25.3.3 `sync` — driving an async to completion synchronously

`sync e` runs an async value to completion in the current (non-async) context and produces `Outcome<Result, E>`. It is the bridge **out** of the async world:

```text
(T-Sync)
AsyncSig(R) = ⊥    Γ; R; L ⊢ e : T_e    AsyncSig(T_e) = ⟨Out, In, Result, E⟩
Out = ()    In = ()
─────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ SyncExpr(e) : Outcome<Result, E>
```

Constraints:
- `sync` must appear **outside** an async procedure — **(Sync-Async-Context-Err)**, `E-CON-0250`.
- The operand's `Out` must be `()` — **(Sync-Out-Err)**, `E-CON-0251`. (A producer that yields meaningful output cannot be `sync`'d; drive it with a `loop` or `fold` instead.)
- The operand's `In` must be `()` — **(Sync-In-Err)**, `E-CON-0252`.
- The operand expression must not *syntactically contain* a `yield` or `yield from` — **(Sync-Yield-Err)** / **(Sync-YieldFrom-Err)**, `E-CON-0212` / `E-CON-0223`.

Dynamically, `sync` loops: while `@Suspended { output = () }`, resume with `()`; on `@Completed { value }`, produce `Outcome::Value(value)`; on `@Failed { error }`, produce `Outcome::Error(error)`.

```ultraviolet
/// Bridges a `Future<Config, ConfigError>` into ordinary control flow.
/// `sync` produces `Outcome<Config, ConfigError>`; the `if … is` inspects the variants.
public procedure bootstrap(reader: const ConfigReader) -> () {
    let outcome: Outcome<Config, ConfigError> = sync loadConfig(reader)
    if outcome is Outcome::Value(config) {
        applyConfig(config)
    } else if outcome is Outcome::Error(err) {
        reportFailure(err)
    }
}
```

#### 25.3.4 `race` — first-to-settle wins

`race` has two modes determined uniformly across its arms. Every arm must use the same handler kind (all plain expressions, or all `yield` handlers); mixing them is **(Race-Handler-Mix-Err)**, `E-CON-0263`. A race needs at least two arms — **(Race-Arity-Err)**, `E-CON-0260`.

```text
RaceMode(arms) =
  { `return`    if every arm handler is RaceReturn(_)
    `yield`     if every arm handler is RaceYield(_)
    ⊥           otherwise   (rejected) }
```

**Return mode** races operands of `Out = ()`, `In = ()`, binds each completed `Result_i` to its arm pattern, runs the matching handler, and produces a common handler type unioned with all error types:

```text
(T-Race)
n ≥ 2    RaceMode = `return`    every operand AsyncSig = ⟨(), (), Result_i, E_i⟩
pat_i binds Result_i    each handler r_i : T_i^r    all T_i^r equal = T_r
──────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ RaceExpr(arms) : T_r | E_1 | … | E_n
```

Handlers that disagree on type are **(Race-Handler-Type-Err)**, `E-CON-0261`. An operand with `Out ≠ ()` is **(Race-Operand-Out-Err)**, `E-CON-0262`. An operand with `Out = ()` but `In ≠ ()` is **(Race-Operand-Err)** (also reported under the composition diagnostics).

**Streaming mode** (`yield` handlers) races producers (`In = ()`), binds each arm's `Out_i` to its pattern, and the whole `race` becomes a `Stream` of the common handler type:

```text
(T-Race-Stream)
n ≥ 2    RaceMode = `yield`    every operand AsyncSig = ⟨Out_i, (), Result_i, E_i⟩
pat_i binds Out_i    each handler yields U_i    all U_i equal = U
──────────────────────────────────────────────────────────────────────────────
Γ; R; L ⊢ RaceExpr(arms) : Stream<U, E_1 | … | E_n>
```

A streaming operand with `In ≠ ()` is **(Race-Stream-Operand-Err)**; disagreeing stream handler types are **(Race-Stream-Handler-Type-Err)**.

Dynamically, return-mode `race` starts all arms concurrently, handles the first to complete or fail, then cancels the rest. Streaming-mode `race` emits each yielded-and-handled value as the next stream output; on resumption the previously yielded arm resumes first, then remaining live arms resume in declaration order; the first to fail cancels the rest.

```ultraviolet
/// Return-mode race: whichever source resolves first wins; the loser is
/// cancelled. Both arms produce `Document`, so the race is `Document | FetchError`.
public procedure fastestFetch(
    cache: const Cache,
    origin: const Origin
) -> Future<Document, FetchError> {
    let result: Document | FetchError = race {
        fetchFromCache(cache)   -> |doc| doc,
        fetchFromOrigin(origin) -> |doc| doc
    }
    if result is doc: Document {
        return doc
    } else {
        return result?
    }
}
```

#### 25.3.5 `all` — join all results

`all { e_1, …, e_n }` runs every operand concurrently and joins their results into a tuple, in expression order, unioned with all error types. Every operand must have `Out = ()` and `In = ()`:

```text
(T-All)
∀ i, AsyncSig(e_i) = ⟨(), (), Result_i, E_i⟩
─────────────────────────────────────────────────────────────────
Γ; R; L ⊢ AllExpr([e_1, …, e_n]) : (Result_1, …, Result_n) | E_1 | … | E_n
```

- An operand with `Out ≠ ()` is **(All-Out-Err)**, `E-CON-0270`.
- An operand with `In ≠ ()` is **(All-In-Err)**, `E-CON-0271`.

Dynamically: start all, wait for all to complete, return the result tuple in expression order; on the first failure, cancel the remaining operations and return that error value.

```ultraviolet
/// Joins three independent futures into a tuple. On any failure, the
/// remaining operations are cancelled and the first error is returned.
public procedure loadAll(
    profile_source: const ProfileSource,
    settings_source: const SettingsSource,
    catalog_source: const CatalogSource
) -> Future<(Profile, Settings, Catalog), LoadError> {
    let joined: (Profile, Settings, Catalog) | LoadError = all {
        loadProfile(profile_source),
        loadSettings(settings_source),
        loadCatalog(catalog_source)
    }
    if joined is bundle: (Profile, Settings, Catalog) {
        return bundle
    } else {
        return joined?
    }
}
```

#### 25.3.6 The async combinators

Five combinators are built-in modal members of `Async` (resolved through `Async`'s built-in modal member lookup, including aliases normalized via `AsyncSig` — they are **not** ad-hoc method-call exceptions). They are invoked with `~>`. Their signatures (the spec writes the callback positions as procedure/closure types; the concrete type spelling is the function-type form `(…) -> …`):

```text
map    : Async<Out, In, Result, E> × (Out) -> U          -> Async<U, In, Result, E>
filter : Async<T, (), (), E>       × (const T) -> bool    -> Async<T, (), (), E>
take   : Async<T, (), (), E>       × usize                -> Async<T, (), (), E>
fold   : Async<T, (), (), E>       × A × (A, T) -> A       -> Future<A, E>
chain  : Future<T, E>              × (T) -> Future<U, E>   -> Future<U, E>
```

- **`map(f)`** transforms each output through `f`, leaving `In`, `Result`, and `E` unchanged. — **(T-Async-Map)**
- **`filter(p)`** keeps only outputs satisfying predicate `p` (a `(const T) -> bool`); requires `In = ()`, `Result = ()`. — **(T-Async-Filter)**
- **`take(n)`** truncates the producer to at most `n` outputs, then completes; requires `In = ()`, `Result = ()`. — **(T-Async-Take)**
- **`fold(init, f)`** consumes the whole producer, accumulating with `f`, and yields a `Future<A, E>` of the final accumulator. — **(T-Async-Fold)**
- **`chain(f)`** sequences futures: when the source `Future<T, E>` completes, `f` produces the next `Future<U, E>`. — **(T-Async-Chain)**

```ultraviolet
/// Pipeline: take the first 5 naturals, drop odd ones, square the rest,
/// then sum. Each combinator returns a fresh wrapper async value.
public procedure sumEvenSquares() -> Future<i32, !> {
    let pipeline: Future<i32, !> =
        naturals(5)
            ~>filter(isEven)
            ~>map(square)
            ~>fold(0, addInto)

    return pipeline
}

procedure isEven(value: const i32) -> bool {
    return value % 2 == 0
}

procedure square(value: i32) -> i32 {
    return value * value
}

procedure addInto(acc: i32, value: i32) -> i32 {
    return acc + value
}
```

For `e~>name(args)`, when `StripPerm(ExprType(e))` is a modal reference to `Async` (after alias normalization) and `name ∈ {map, filter, take, fold, chain}`, typing is derived by the combinator rules above. A conforming implementation must resolve these through built-in modal member lookup and must not treat them as ordinary ad-hoc methods.

#### 25.3.7 `until` — a source-specified surface on `shared` values

`until` is a method-call surface on `shared` values with the source-specified type (the callbacks are written here in function-type form; `Future<R>` means `Future<R, !>`):

```text
until : shared T × ((const T) -> bool) × ((unique T) -> R) -> Future<R>
```

It registers a predicate/action pair: when `pred(shared_value)` becomes true, it acquires a Write key for the target path, runs `action`, and completes the future with the result; otherwise it registers a waiter and suspends with `@Suspended { output = () }`, re-evaluating registered waiters on key release.

#### 25.3.8 Composition diagnostics

| Code | Condition |
| --- | --- |
| `E-CON-0212` | `yield` inside a `sync` expression |
| `E-CON-0223` | `yield from` inside a `sync` expression |
| `E-CON-0240` | Iteration over an async with `In ≠ ()` |
| `E-CON-0250` | `sync` inside an async-returning procedure |
| `E-CON-0251` | `sync` operand has `Out ≠ ()` |
| `E-CON-0252` | `sync` operand has `In ≠ ()` |
| `E-CON-0260` | `race` with fewer than 2 arms |
| `E-CON-0261` | `race` arms have incompatible types |
| `E-CON-0262` | Non-streaming `race` operand has `Out ≠ ()` |
| `E-CON-0263` | Mixed yield/non-yield handlers in `race` |
| `E-CON-0270` | `all` operand has `Out ≠ ()` |
| `E-CON-0271` | `all` operand has `In ≠ ()` |

### 25.4 The Async State Machine Model

An async procedure compiles to a **modal state machine**. There is no surface syntax here beyond ordinary procedure declarations, calls, and `~>resume(…)` on `@Suspended`; this section is the operational model behind the keywords.

#### 25.4.1 What "async procedure" means structurally

A procedure is async iff `AsyncSig(R) ≠ ⊥`. Its suspension expressions are the `yield`/`yield from` occurrences:

```text
SuspendExpr(e) ⇔ e = YieldExpr(_, _) ∨ e = YieldFromExpr(_, _)
```

Calling an async procedure is an **async creation expression**: a call, method call, or `race` whose result type has an `AsyncSig`. Such expressions *capture* their argument values (including, for a method call, the receiver) into a fresh async frame:

```text
AsyncCreateExpr(Call(_, _))        ⇔ AsyncSig(ExprType(Call(_, _))) ≠ ⊥
AsyncCreateExpr(MethodCall(_, _, _)) ⇔ AsyncSig(ExprType(MethodCall(_, _, _))) ≠ ⊥
AsyncCreateExpr(RaceExpr(_))       ⇔ AsyncSig(ExprType(RaceExpr(_))) ≠ ⊥
AsyncCreateExpr(_)                 ⇔ false
```

#### 25.4.2 The async frame

The compiled async frame stores exactly three things:
- every binding **live across a suspension** (a binding for which a control-flow path runs from a suspension point to a later use without redefining it);
- the current **resumption point** (which suspension to resume at);
- implementation fields required by the runtime.

Formally, the frame slots are the captured arguments plus the live-across-suspend set:

```text
GenPoints(proc) = [g_0, …, g_n]   (suspension expressions in source order)
FrameSlots(proc) = CaptureSet(proc) ∪ LiveAcrossSuspend(proc)
```

This is the **zero-cost suspension** property: only state that genuinely must survive a yield is stored. A binding that is never read after any suspension is *not* placed in the frame; it stays an ordinary local. The frame is sized to the live set, not to the whole procedure.

#### 25.4.3 Settlement — how the three states are produced

The body's control flow maps onto the three states:

- `yield` / `yield from` → `Async@Suspended { output = v }`.
- `return v` → `Async@Completed { value = v }`.
- error propagation via `?` → `Async@Failed { error = e }`.

A call to an async procedure:
1. evaluates arguments left-to-right;
2. allocates a fresh async frame, capturing required arguments and initializing the resumption point to procedure entry;
3. executes the body until it suspends, completes, or fails;
4. evaluates to the produced `Async` modal state.

`a~>resume(input)` on `a : Async@Suspended` resumes at the stored resumption point with `input` bound to the suspended `yield` expression, runs to the next suspension/completion/failure, and returns the resulting `Async` state.

When an async computation **fails**, the runtime: captures the error value, runs `defer` blocks in reverse order, runs `Drop` implementations for live bindings, then transitions to `@Failed { error }`. This is the same orderly teardown as a synchronous failing return.

#### 25.4.4 Error propagation inside async procedures

`?` inside an async procedure is typed by the async propagate rules. When the enclosing error type `E` is fallible (`E ≠ !`), `?` extracts the success member and routes the error into `@Failed`:

```text
(T-Async-Try)
AsyncSig(R) = ⟨Out, In, Result, E⟩    E ≠ !    Γ; R; L ⊢ e : U
SuccessMemberAsync(E, U) = T_s
──────────────────────────────────────────────────────────────
Γ; R; L ⊢ Propagate(e) : T_s
```

`SuccessMemberAsync(E, U)` picks the single union member of `U` that is *not* a subtype of `E`; every other member must be a subtype of `E` and is routed into `@Failed`.

If the async procedure is **infallible** (`E = !`), using `?` is a hard error — **(Async-Try-Infallible-Err)**, `E-CON-0230` — because there is no `@Failed` state to route into. An infallible async procedure (e.g. any `Sequence<T>` or a `Future<T, !>`) may not propagate.

#### 25.4.5 Capture and escape provenance

Async creation captures argument provenance. Two provenance constraints apply:

- **(Async-Capture-Err)** / **(P-Async-Create)** — a captured argument must outlive the async frame. If any captured value's provenance is shorter-lived than the frame, the creation is rejected. — `E-CON-0280` ("captured binding does not outlive async").
- **(Prov-Async-Escape-Err)** — assigning an async value (`AsyncSig(ExprType(e)) ≠ ⊥`) into a longer-lived place than its provenance is rejected: an async operation may not escape its region. — `E-CON-0281`.

A **large-capture warning** fires when the total captured size exceeds the widen threshold:

```text
ASYNC_LARGE_CAPTURE_THRESHOLD_BYTES = WIDEN_LARGE_PAYLOAD_THRESHOLD_BYTES
AsyncCaptureWarnCond(e) ⇔
  AsyncCreateExpr(e) ∧ AsyncCaptureSize(AsyncCaptureArgs(e)) > ASYNC_LARGE_CAPTURE_THRESHOLD_BYTES
```

This is **(Warn-Async-LargeCapture)**, `W-CON-0201` — a performance advisory, not an error.

#### 25.4.6 Lowering (informative)

Lowering produces, per async procedure, an `AsyncFrameInitIR` that evaluates arguments left-to-right, allocates the frame, copies/moves captures into `FrameSlots`, initializes the generation point to `g_0`, and enters an `AsyncResumeSwitchIR`. The resume switch dispatches on the stored generation point to the matching resumption label. `@Suspended`, `@Completed`, and `@Failed` lower through `AsyncSuspendStateIR`, `AsyncCompleteStateIR`, and `AsyncFailStateIR`. `AsyncFailStateIR` runs `defer` blocks and drops live frame slots before materializing `@Failed { error }`; **when `E = !` it is unreachable and is not emitted at all**, which is why the `@Failed` state and its storage are omitted from infallible async layouts.

#### 25.4.7 State-machine diagnostics

| Code | Severity | Condition |
| --- | --- | --- |
| `W-CON-0201` | Warning | Large captured state (performance) |
| `E-CON-0280` | Error | Captured binding does not outlive async |
| `E-CON-0281` | Error | Async operation escapes its region |
| `E-CON-0203` | Error | `result` type mismatch with `Result` parameter |
| `E-CON-0230` | Error | Error propagation in infallible async procedure |

`E-CON-0203` and `E-CON-0230` are owned by the async diagnostics supplement (§21.6); the others are local to the state-machine section.

### 25.5 Async–Key Integration

Async suspension interacts directly with the key system. The governing principle: **at suspension, a task releases access rights, and other tasks may acquire keys to the same paths during the suspension period.** This is why holding a key across a bare suspension is forbidden — it would let one task hold exclusive access indefinitely while parked.

#### 25.5.1 The three key restrictions

At a program point `p`, let `Γ_keys(p)` be the set of held keys. Then:

- `wait` is ill-formed when `Γ_keys(p) ≠ ∅`. — `E-CON-0133` ("`wait` while key is held").
- `yield` with `release_opt = ⊥` is ill-formed when `Γ_keys(p) ≠ ∅`. — `E-CON-0213` ("`yield` while key is held without `release`").
- `yield from` with `release_opt = ⊥` is ill-formed when `Γ_keys(p) ≠ ∅`. — `E-CON-0224` ("`yield from` while key is held without `release`").

`wait` has **no** `release` form: you simply must not hold a key when you `wait`. Restructure so keys are released before the `wait`.

#### 25.5.2 `yield release` — suspend while holding keys

To suspend legally while keys are held, use the `release` modifier. `yield release e` (and `yield release from e`):
1. evaluates `e`;
2. snapshots and releases all held keys (recording the set);
3. suspends with `@Suspended { output }`;
4. on resumption, **reacquires** the recorded keys in canonical order *before* control reaches the continuation.

Release and reacquisition use canonical key ordering to preserve the global lock-acquisition discipline and avoid deadlock. The reacquisition (`ReacquireHeldKeysIR`) is prepended to the resumption target, so the continuation always runs with its keys re-held.

A key acquisition block uses a decorated head such as `%write` or `%read`
(not a bare `key` keyword) over one or more key paths:

```ultraviolet
/// Holds a Write key to update shared progress, then `yield release`s so
/// other tasks may read the path while this producer is parked. On resume,
/// the key is reacquired automatically before the next statement runs.
public procedure trackedProducer(total: usize, progress: shared Progress) -> Sequence<Chunk> {
    var index: usize = 0
    loop index < total {
        let chunk: Chunk = produceChunk(index)

        %write progress {
            progress.completed = index
        }

        yield release chunk
        index = index + 1
    }
    return ()
}
```

Note that `progress` is declared `progress: shared Progress` — the `shared` permission is part of the parameter's *type*, not a separate parameter modifier.

#### 25.5.3 Shared-capturing closures with `yield`

A closure that **captures `shared` bindings and contains a `yield`** is additionally constrained: it must not hold keys across the yield point unless `release` is present. A bare `yield` at a key-holding point inside such a closure is rejected by **(A-Closure-Yield-Keys-Err)**:

```text
(A-Closure-Yield-Keys-Err)
C = ClosureExpr(params, ret_type_opt, body)    YieldExpr(_, _) ∈ body    SharedCaptures(C) ≠ ∅
YieldExpr(⊥, _) at program point p within body    Γ_keys(p) ≠ ∅
──────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ C ⇑
```

#### 25.5.4 Staleness across `yield release`

Because keys are released across a `yield release`, other tasks may mutate the shared paths while you are suspended. Bindings *derived from shared data before* a `yield release` are therefore **potentially stale** after resumption. The staleness warning applies to such bindings unless explicitly suppressed with `#stale_ok`. Lowering marks these bindings with `StaleValueMarkIR`; they remain usable, but continue to trigger the staleness warning until `#stale_ok` is applied. Re-read shared state after resuming rather than trusting a pre-yield snapshot.

#### 25.5.5 Async capability requirements

Async operations require capabilities according to what they do:

| Category | Capability required |
| --- | --- |
| Pure sequence (no I/O, no timing) | None |
| I/O operation | A capability providing the invoked I/O method |
| Timing | `System` |
| Async runtime (scheduling, reactor) | `Reactor` |

A pure `Sequence<T>` that only computes and yields needs **no** capability. The moment an async procedure performs I/O, sleeps/times out, or drives a reactor-backed scheduler, it must receive the corresponding capability as a parameter. This is consistent with the style guide's narrow-authority rule: pass exactly `Reactor` or `System` (or the specific I/O capability) only to the procedures that genuinely use them.

#### 25.5.6 Async–key diagnostics

| Code | Condition |
| --- | --- |
| `E-CON-0133` | `wait` while a key is held |
| `E-CON-0213` | `yield` while a key is held (without `release`) |
| `E-CON-0224` | `yield from` while a key is held (without `release`) |

### Idioms & Best Practices

- **Model the protocol in the type parameters.** Choose the narrowest alias that fits: `Sequence<T>` for an infallible producer, `Stream<T, E>` for a fallible one, `Future<T, E>` for a single eventual value, `Pipe<In, Out>` for a request/response channel, `Exchange<T>` for a symmetric one. Reach for the raw `Async<Out, In, Result, E>` only when no alias matches. This is the async expression of the style guide's preference for using the type system over weaker runtime validation, and for `modal` types over informal state.
- **Use `var` for stepped/accumulated state, `let` everywhere else.** Any async loop counter, manual-stepping handle, or fold accumulator that you reassign must be `var`. Reserve `let` for values you bind once. Never write `let mut` — it is not Ultraviolet syntax.
- **Keep async procedures and combinator callbacks `camelCase`; locals and parameters `snake_case`.** `naturals`, `loadConfig`, `trackedProducer` are procedures; `current`, `index`, `chunk`, `profile_source` are locals/parameters. Yield/result *types* stay `PascalCase` (`Chunk`, `Config`).
- **Make infallibility explicit and lean on it.** An async procedure with `E = !` cannot use `?` and emits no `@Failed` storage. If a producer truly cannot fail, declare it `Sequence<T>` / `Future<T, !>` so the compiler removes the failure path entirely. Conversely, if you find yourself wanting `?`, the procedure's error type must be a real (non-`!`) type.
- **Drive `In = ()` producers with `loop … in`; step `In ≠ ()` machines by hand.** A `loop` is only legal over an async with `In = ()`. For a `Pipe` or any machine that consumes resume input, inspect `if x is @Suspended`, read `x.output`, and call `~>resume(input)` explicitly on a `var` you reassign. To step an async received as a parameter, `move` it into a `var` first, because parameters are immutable.
- **Use `sync` only at the boundary, never inside async code.** `sync` is the synchronous bridge *out* of async; it is rejected inside an async procedure. Inside async code, compose with `yield from`, `loop … in`, `race`, `all`, or the combinators instead.
- **Release keys before suspending; re-read shared state after.** Prefer scoping `%write`/`%read` blocks tightly so no key is live at a `wait`/`yield`. When you must hold across a yield, use `yield release` and re-read shared data after resumption rather than trusting a pre-yield value (which the staleness analysis will flag).
- **Pass only the capability the async procedure uses.** A pure sequence needs none; add `System` only for timing and `Reactor` only for runtime-backed scheduling. Do not thread a broad context through async procedures.
- **Keep captures small.** The frame stores everything captured and everything live across a suspension. Capture handles and small descriptors, not large buffers, to stay under the large-capture warning and keep frames cheap.

### Pitfalls & Diagnostics

- **Bare `Async` is ill-formed.** Writing `Async` with no arguments triggers **(WF-Async-Path-Err)**. Always apply the type, even if only `Async<T>`.
- **`return` is checked against `Result`, not the async type.** In `procedure p() -> Sequence<i32>`, `return e` is checked against `()` (the `Result` of `Sequence`), not `Sequence<i32>`. A mismatch is `E-CON-0203`. To *produce* values you `yield`; you `return` only the `Result`.
- **`let mut` is not a thing; mutable bindings use `var`.** A reassigned async loop counter or stepping handle declared with `let` is a reassignment of an immutable binding. Use `var`. The keyword `mut` exists only as a raw-pointer qualifier.
- **Parameters cannot be reassigned.** Parameters are immutable `let` bindings, and the only parameter mode is `move`; there is no `mut` parameter. To step or mutate an async (or any value) you received as a parameter, `move` it into a local `var`.
- **Enum/variant construction uses `::`, not `.`.** Write `Ack::Pending { queued: pending }`, never `Ack.Pending { … }`. The `.` form is field/method access; `::` is the path/variant separator.
- **Key blocks use decorated `%` heads, not a `key` keyword.** The acquisition head is `%write path { … }` (or `%read`, `%release read|write`, `%speculative write`). There is no `key write` / `key read` block form.
- **`shared` is a type permission, not a parameter modifier.** Write `progress: shared Progress`. Do not prefix the parameter name with a bare `shared`.
- **`yield`/`yield from`/`sync` are context-sensitive.** `yield` outside an async procedure is `E-CON-0210`; `yield from` outside is `E-CON-0220`; `sync` *inside* an async procedure is `E-CON-0250`. `yield` whose operand mistypes against `Out` is `E-CON-0211`; `yield from` whose `Out`/`In`/error don't match is `E-CON-0221`/`E-CON-0222`/`E-CON-0225`.
- **`wait` takes a handle, not an async value.** `wait` operates on `Spawned<T>` or `Tracked<T, E>`. Passing an `Async`/`Future`/`Sequence` value is `E-CON-0132`. To run an async value, use `sync`, a `loop`, or manual `resume` — not `wait`.
- **`?` in an infallible async procedure is an error.** Using `?` when `E = !` is `E-CON-0230` (**(Async-Try-Infallible-Err)**). Give the procedure a real error type, or settle errors without propagation.
- **`loop … in` over an `In ≠ ()` machine is rejected.** `E-CON-0240`. Such machines must be stepped manually with `~>resume(input)`.
- **`sync` cannot consume a producer.** A `sync` operand must have `Out = ()` (`E-CON-0251`) and `In = ()` (`E-CON-0252`), and must not syntactically contain `yield`/`yield from` (`E-CON-0212`/`E-CON-0223`). To run a producer to a value, fold or loop over it.
- **`race` arms must agree.** Fewer than two arms is `E-CON-0260`; mixing plain and `yield` handlers is `E-CON-0263`; disagreeing handler types is `E-CON-0261`; a non-streaming arm with `Out ≠ ()` is `E-CON-0262`. Keep all arms in one mode with one common result type.
- **`all` operands must be joinable futures.** Each operand needs `Out = ()` (`E-CON-0270`) and `In = ()` (`E-CON-0271`).
- **Holding a key across a bare suspension fails.** `wait` with a key held is `E-CON-0133` (and has no `release` escape — release the key first). Bare `yield`/`yield from` with a key held is `E-CON-0213`/`E-CON-0224`; use `yield release` / `yield release from`. A `shared`-capturing closure with a bare `yield` at a key-holding point is **(A-Closure-Yield-Keys-Err)**.
- **Captures must outlive the frame, and async values must not escape their region.** A capture that does not outlive the async is `E-CON-0280`; assigning an async value into a longer-lived place is `E-CON-0281`. A large capture is the `W-CON-0201` performance warning — shrink the captured set.
- **Post-`yield release` values may be stale.** Bindings derived from shared data before a `yield release` carry the staleness warning after resumption; suppress with `#stale_ok` only when you have *verified* the value is still valid, otherwise re-read.
