## 24. Structured Parallelism

Ultraviolet expresses parallelism as a *structured*, fork-join construct. Every parallel task is created inside a lexical `parallel` block bound to an explicit execution domain, and the block does not complete until every task it forked has settled. There are no detached threads, no implicit global thread pools reachable from arbitrary code, and no way to leak a running task past the block that owns it. The result is parallelism whose lifetime, cancellation scope, and panic propagation are all bounded by the same syntactic region.

This chapter covers the four surface forms of structured parallelism — the `parallel` block, the `spawn` expression, the `dispatch` parallel loop, and the `wait` expression — together with execution domains, capture rules, cancellation, panic propagation, and the determinism and nesting guarantees. The key-system interactions referenced here (`shared` captures, `key` clauses, key-held restrictions) are defined in *Chapter 19, The Key System*; the `Spawned<T>`, `Tracked<T, E>`, and `CancelToken` modal types are built-in modals in the sense of *Chapter 13, Modal Types*; and `wait` is shared with the asynchronous forms of *Chapter 21*.

A note on call syntax before any examples. Ultraviolet has exactly one method-call operator, `~>` (`postfix_expr "~>" identifier "(" argument_list? ")"`, Appendix B.16.3). The `.` punctuator is *field access only*; it is not a method-call operator. Throughout this chapter `context~>cpu()` is a method call, while `context.heap` is a field access. The execution-domain selectors `cpu`, `gpu`, and `inline` are methods of `Context`; the capability roots `io`, `net`, `heap`, `sys`, `reactor`, and `time` are fields of `Context`.

### 24.1 Parallel Blocks (§20.1)

A `parallel` block introduces a structured-concurrency scope bound to one execution domain. `spawn` and `dispatch` are well-formed only inside such a scope.

#### 24.1.1 Syntax

The block is a primary expression. Its canonical grammar (Appendix B; §20.1.1) is reproduced verbatim:

```ebnf
parallel_block       ::= "parallel" domain_expr parallel_option_list? block
domain_expr          ::= expression
parallel_option_list ::= "[" parallel_option ("," parallel_option)* "]"
parallel_option      ::= "cancel" ":" expression
                       | "name" ":" string_literal
                       | "workgroup" ":" dim3_const
                       | "workgroups" ":" dim3_const
dim3_const           ::= "(" ct_usize_expr "," ct_usize_expr "," ct_usize_expr ")"
ct_usize_expr        ::= expression
```

`parallel` is a reserved keyword; it is one of the operand-introducing keywords in `BeginsOperand` (the lexer/parser preliminaries), alongside `spawn`, `dispatch`, and `yield`. The `domain_expr` is parsed by `ParseExpr_NoBrace` — a non-brace expression — so that the opening `{` of the block body is never mistaken for a record literal or a nested block. The option list, when present, is bracketed and comma-separated; each option is one of `cancel`, `name`, `workgroup`, or `workgroups`. The option lexemes `cancel`, `name`, `workgroup`, and `workgroups` are *fixed identifiers* (`FixedIdent_Parallel`), not keywords — they are tokenized as identifiers and disambiguated purely by position inside the option list.

#### 24.1.2 Options and their static requirements (§20.1.4)

Each option is validated by `BlockOptOk`:

- `name: "..."` — a string literal label for the block. Always well-formed (`BlockOptOk(Name(_)) ⇔ true`). Used for diagnostics and tooling.
- `cancel: e` — `e` MUST have type `CancelToken` (`BlockOptOk(Cancel(e)) ⇔ G ⊢ e : TypePath(["CancelToken"])`). Attaches a cancellation token to the block; see §24.6.
- `workgroup: (x, y, z)` and `workgroups: (x, y, z)` — each operand is a `dim3_const`: a parenthesized triple of compile-time `usize` constants, **each strictly greater than zero**. The validation rule is:

  ```text
  G ⊢ Dim3Const((e_1, e_2, e_3)) ⇓ (x, y, z) ⇔
    G ⊢ e_1 : TypePrim("usize") ∧ G ⊢ ConstLen(e_1) ⇓ x ∧ x > 0 ∧
    G ⊢ e_2 : TypePrim("usize") ∧ G ⊢ ConstLen(e_2) ⇓ y ∧ y > 0 ∧
    G ⊢ e_3 : TypePrim("usize") ∧ G ⊢ ConstLen(e_3) ⇓ z ∧ z > 0
  ```

  `workgroup`/`workgroups` configure GPU topology only; on CPU and inline domains they are inert. A malformed triple is rejected by `Dim3Const-Err` (`E-CON-0103`).

#### 24.1.3 Static semantics (§20.1.4)

The typing rule is **(T-Parallel)**:

```text
Γ ⊢ D : `$ExecutionDomain`    DomainCtorOk(D)    BlockOptsOk(opts)
Γ_P = Γ[parallel_context ↦ D]    Γ_P ⊢ B : T
────────────────────────────────────────────────────────────────
Γ ⊢ `parallel` D opts {B} : T
```

A parallel block is well-formed only if all three of the following hold:

1. the domain expression evaluates to a value implementing `ExecutionDomain` (otherwise `Parallel-Domain-Param-Err`, `E-CON-0102`);
2. every enclosed `spawn` and `dispatch` is lexically inside this (or a nested) parallel block — the binding `parallel_context ↦ D` is what makes them legal (otherwise `E-CON-0101` for `spawn`, `E-CON-0140` for `dispatch`);
3. the capture constraints of §24.3 are satisfied.

The block evaluates to the type `T` of its body. A `parallel` block is therefore an ordinary expression and may appear in expression position, including as the right-hand side of a binding.

`DomainCtorOk` accepts any of the recognized constructor shapes (`cpu`/`gpu`/`inline` with the argument shapes of §24.2.1) and, for any expression that is *not* a recognized constructor, accepts it as long as it independently types as `$ExecutionDomain`:

```text
DomainCtor(MethodCall(ctx, name, args)) ⇔ name ∈ {cpu, gpu, inline}
DomainCtorOk(D) ⇔ ¬DomainCtor(D)        (when D is not a recognized constructor call)
```

#### 24.1.4 Dynamic semantics: fork-join (§20.1.5)

Evaluating `parallel D opts { B }` (**EvalSigma-Parallel**) proceeds as:

1. Evaluate the domain expression `D` to a domain value `d`.
2. Initialize the parallel state: `ParallelInit(d, opts) ⇓ {Domain: d, Handles: [], CancelToken: CancelOpt(opts)}`, where `CancelOpt(opts)` is the token from a `cancel` option or `⊥` if none is present.
3. Evaluate the body `B` with `parallel_context` bound to that state. Each `spawn`/`dispatch` appends handles to `Handles`.
4. **Join.** `AwaitSpawned(pstate_n, σ)` blocks until *every* handle reaches `Ready` or `Failed`. The block does not complete while any forked task is still running.
5. The block yields the body's value `v_body` if no task failed; if any task failed, the block propagates a panic per §24.7.

This is the defining guarantee: **no task created inside the block outlives the block.** Control-flow effects (`return`, `break`, `continue`) raised by the body still force the join before they leave the block (**EvalSigma-Parallel-Body-Ctrl**), and a control effect or panic raised while evaluating the *domain expression itself* short-circuits before any state is created (**EvalSigma-Parallel-Domain-Ctrl**).

For a GPU domain, the block instead runs the GPU pipeline (`GpuInit` → `EvalGpuBody` → `GpuExecute` → `GpuJoin`, **EvalSigma-GPU-Parallel**) and applies the workgroup topology computed from the options by `ComputeTopologyParallel`.

#### 24.1.5 Worked example

```ultraviolet
/// Run two independent analyses concurrently on the CPU domain and combine them.
procedure analyzeBatch(context: Context, batch: const [Sample]) -> Report {
    let report: Report = parallel context~>cpu() [name: "analyze-batch"] {
        let totals: Spawned<Totals> = spawn {
            computeTotals(batch)
        }
        let extremes: Spawned<Extremes> = spawn {
            computeExtremes(batch)
        }

        Report {
            totals: wait totals,
            extremes: wait extremes,
        }
    }
    return report
}
```

The block forks two tasks, waits for both, and assembles a `Report`. Even if the body did not explicitly `wait` on a handle, the implicit join at the block boundary would still wait for that task before `analyzeBatch` returned.

#### 24.1.6 Diagnostics (§20.1.7)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0101` | Error | `spawn` outside a parallel block |
| `E-CON-0102` | Error | Domain expression not `ExecutionDomain` (`Parallel-Domain-Param-Err`) |
| `E-CON-0103` | Error | Invalid parallel domain or option parameter type (`Dim3Const-Err`) |

### 24.2 Execution Domains (§20.2)

A *domain* is the value that tells a `parallel` block where and how to run. Domains are obtained by calling `Context` methods; there is no special domain syntax.

#### 24.2.1 Syntax and constructors

Execution-domain values use ordinary method-call syntax via the `~>` method-call operator (Appendix B; `method_call_expr ::= postfix_expr "~>" identifier "(" argument_list? ")"`). The three constructors are `Context` methods (§20.2.4), each declared with a `const` receiver and returning `$ExecutionDomain`:

```ultraviolet
context~>cpu()
context~>gpu()
context~>inline()
```

The recognized domain constructors and their accepted argument shapes are fixed by `DomainCtorOk` (§20.1.4):

```text
DomainCtor(MethodCall(ctx, name, args)) ⇔ name ∈ {cpu, gpu, inline}
DomainCtorOk(MethodCall(ctx, cpu, []))
DomainCtorOk(MethodCall(ctx, cpu, [mask]))       ⇔ G ⊢ mask : TypePath(["CpuSet"])
DomainCtorOk(MethodCall(ctx, cpu, [mask, prio])) ⇔ G ⊢ mask : TypePath(["CpuSet"]) ∧ G ⊢ prio : TypePath(["Priority"])
DomainCtorOk(MethodCall(ctx, gpu, []))
DomainCtorOk(MethodCall(ctx, inline, []))
```

So the legal forms are:

| Form | Meaning (§20.2.4) |
| --- | --- |
| `context~>cpu()` | The domain-default CPU execution domain. |
| `context~>cpu(mask)` | A CPU domain restricted to the cores set in the `CpuSet` mask. |
| `context~>cpu(mask, priority)` | A CPU domain restricted to `mask` with default task `Priority`. |
| `context~>gpu()` | The default GPU execution domain. |
| `context~>inline()` | The inline execution domain. |

Supporting types:

- `CpuSet` is an alias of `u64` interpreted as a CPU-bitset mask. A set bit *i* admits worker core *i*; an all-zero mask falls back to the domain default.
- `Priority` is an enum with variants `Low`, `Normal`, and `High`.

An expression that *is* a recognized `cpu`/`gpu`/`inline` call but supplies the wrong argument types (e.g. `context~>cpu(some_i32)`) is rejected by `Parallel-Domain-Param-Err` (`E-CON-0102`). Any expression that is *not* a recognized constructor is accepted by `DomainCtorOk` as long as it independently types as `$ExecutionDomain` — this is how a custom or forwarded domain value passes the check.

`ExecutionDomain` is a dispatchable class used for heterogeneous domain handling. The `$ExecutionDomain` notation denotes the dynamic (dispatchable) form of that class; the constructors above return `TypeDynamic("ExecutionDomain")`.

#### 24.2.2 Domain behaviors

**Inline domain (§20.2.5).** The inline domain runs all work eagerly and sequentially on the calling task: `spawn { e }` evaluates `e` immediately and blocks until completion; `dispatch i in range { e }` runs as a sequential loop; no actual parallelism occurs. Capture and permission rules are still enforced, so inline is a faithful single-threaded execution of the same program — ideal for deterministic testing and debugging.

**CPU domain.** Work items are scheduled across worker cores subject to the `CpuSet` mask and `Priority`. Within each CPU domain queue, ready work items are dequeued in ascending `TaskId` (§20.8.5), which is the basis of CPU determinism (§24.8).

**GPU domain.** A GPU `parallel` block launches a kernel. Work-items are organized into a three-dimensional hierarchy of workgroups (`GpuTopology = ⟨GlobalSize, WorkgroupSize, NumWorkgroups⟩`). For a parallel block the topology comes from the block options via `ComputeTopologyParallel`:

```text
DEFAULT_GPU_WORKGROUP  = (64, 1, 1)
DEFAULT_GPU_WORKGROUPS = (1, 1, 1)
```

with `GlobalSize` the component-wise product of workgroup size and workgroup count. A GPU dispatch instead derives `NumWorkgroups` from the iteration bounds via `ComputeTopologyDispatch` (`groups = CeilDiv(|bounds|, volume)`). A topology is valid (`TopologyValid`) only if every workgroup dimension is positive, the product `WorkgroupSize.0 × WorkgroupSize.1 × WorkgroupSize.2 ≤ MAX_WORKGROUP_SIZE` (= 1024), and `GlobalSize` is the component-wise product of `WorkgroupSize` and `NumWorkgroups`; otherwise `WorkgroupSize-Err` (`E-CON-0157`).

#### 24.2.3 GPU intrinsics, memory, and safety

Inside a GPU context (`GpuContext(Γ)`, which holds when the enclosing `parallel_context` is a GPU domain), the following nullary intrinsics are available (`GpuIntrinsicTable`). Each is an ordinary call expression with no arguments:

| Intrinsic | Return type |
| --- | --- |
| `gpu_global_id()`, `gpu_local_id()`, `gpu_workgroup_id()`, `gpu_workgroup_size()`, `gpu_global_size()`, `gpu_num_workgroups()` | `(usize, usize, usize)` |
| `gpu_linear_id()` | `usize` |
| `gpu_barrier()`, `gpu_memory_barrier()`, `gpu_workgroup_barrier()` | `()` |

Calling any of these outside a GPU context is an error: the three barriers raise `Barrier-Outside-Err` (`E-CON-0156`); the other intrinsics raise `GpuIntrinsic-Outside-Err` (`E-CON-0154`). `gpu_barrier()` blocks until all work-items in the workgroup reach the barrier, after which every shared-memory write by that workgroup is visible to all its work-items. Reaching a barrier under non-uniform (divergent) control flow is rejected by `Barrier-Divergence-Err` (`E-CON-0158`).

GPU pointers carry their address space in the type: `GpuPtr<T, S>` with `S ∈ {Global, Shared, Private}`. The type is parsed by the ordinary generic type parser with exactly two arguments; the second argument MUST name `Global`, `Shared`, or `Private`. Visibility is enforced dynamically (`GpuMemVisible`): `Global` is visible everywhere; `Shared` only within the same workgroup; `Private` only to the owning work-item. An out-of-scope dereference raises `GpuPtr-Deref-Err` (`E-CON-0150`); an address-space mismatch in typing raises `GpuPtr-AddrSpace-Err` (`E-TYP-2641`).

Only **GPU-safe** types may cross into GPU code. `GpuSafeType(T)` holds iff `¬ProhibitedGpuType(T)` and all components are GPU-safe (`GpuSafeComponents(T)`, which requires `BitcopyType(T)` and recursive safety of every element). The GPU-safe primitives are:

```text
GpuSafePrimTypes = {i8, i16, i32, i64, u8, u16, u32, u64, isize, usize, f16, f32, f64, bool, ()}
```

Prohibited types (`ProhibitedGpuType`) include `$`-dynamic types, `Context`, capability types, managed `string`/`bytes` (`@Managed`), valid safe pointers (`Ptr@Valid`), modal-state types, and modal-reference types. Arrays, tuples, raw pointers, permission-wrapped types, and `Bitcopy` records/enums of GPU-safe components are themselves GPU-safe; `string@View` and `bytes@View` are GPU-safe. A non-GPU-safe type raises `GpuSafeType-Err` (`E-TYP-2640`); a generic record/enum used GPU-safely whose payload type parameters are not bounded by `<T <: GpuSafe>` or a subclass of `GpuSafe` raises `GpuSafe-Generic-Unbounded-Err` (`E-TYP-2642`). **The key system is not available within GPU execution contexts** — a key block in a GPU context is rejected (`E-CON-0155`).

#### 24.2.4 Domain example

```ultraviolet
/// Pin a latency-sensitive pipeline to a fixed core set at high priority.
procedure runPinned(context: Context, mask: CpuSet, work: const WorkSet) -> Summary {
    let domain: $ExecutionDomain = context~>cpu(mask, Priority::High)
    let summary: Summary = parallel domain [name: "pinned-pipeline"] {
        let stage_a: Spawned<StageA> = spawn { runStageA(work) }
        let stage_b: Spawned<StageB> = spawn { runStageB(work) }
        combineStages(wait stage_a, wait stage_b)
    }
    return summary
}
```

#### 24.2.5 Diagnostics (§20.2.7)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0150` | Error | Host/heap memory access in GPU code (`GpuPtr-Deref-Err`) |
| `E-CON-0154` | Error | GPU intrinsic outside GPU context (`GpuIntrinsic-Outside-Err`) |
| `E-CON-0155` | Error | Key block in GPU context |
| `E-CON-0156` | Error | Barrier outside workgroup context (`Barrier-Outside-Err`) |
| `E-CON-0157` | Error | Workgroup size exceeds device limit (`WorkgroupSize-Err`) |
| `E-CON-0158` | Error | Non-uniform control flow at barrier |
| `E-CON-0159` | Error | Invalid `dim3_const` in GPU topology option |
| `E-TYP-2640` | Error | Type not `GpuSafeType` (`GpuSafeType-Err`, `GpuSafe-Record-Field-Err`) |
| `E-TYP-2641` | Error | `GpuPtr` address-space mismatch (`GpuPtr-AddrSpace-Err`) |
| `E-TYP-2642` | Error | Generic `GpuSafeType` with unconstrained parameter (`GpuSafe-Generic-Unbounded-Err`) |

### 24.3 Capture Semantics (§20.3)

`spawn` and `dispatch` bodies are closures; what they may capture from the enclosing scope is governed by *permission*. This section introduces no new surface syntax — capture follows the closure rules of *Chapter 16* — but adds parallel-specific restrictions.

#### 24.3.1 CPU capture rules (§20.3.4)

For CPU and inline parallel bodies, capture by permission is:

- **`const`** bindings MAY be captured by reference (**Parallel-Closure-Capture-Const**).
- **`shared`** bindings MAY be captured by reference; concurrent access is synchronized by the key system of *Chapter 19* (**Parallel-Closure-Capture-Shared**).
- **`unique`** bindings MUST NOT be captured by closures used in `spawn` or `dispatch` bodies (**Parallel-Closure-Capture-Unique-Err**). Implicitly capturing a `unique` binding raises `E-CON-0120`.

There is one disciplined exception that lets ownership cross into a child task. A `unique` binding may be *moved* into exactly one child task when it is bound in the enclosing parallel body, the move is explicit, and the enclosing parallel capture analysis selects this child as the unique recipient (**Parallel-Closure-Capture-Unique-Move-Ok**, requiring `ExplicitMove(x) ∧ OuterParallelBinding(Γ, x) ∧ FirstChildMove(Γ, x)`). Moving the same outer `unique` binding into a *second* task — or otherwise failing the `FirstChildMove` selection — raises `Parallel-Closure-Capture-OuterMove-Err` (`E-CON-0122`). Moving an already-moved binding raises `E-CON-0121`.

All closures in parallel contexts are classified as *local* closures for Chapter 19 key analysis. A `spawn` expression is **forbidden in the body of an escaping closure** (**Parallel-Escaping-Closure-Spawn-Err**, `E-CON-0131`).

#### 24.3.2 GPU capture rules (§20.3.4)

GPU capture is stricter (`GpuCaptureOk`). Inside a GPU context:

- A **`const`** binding may be captured iff its type is `GpuSafeType` and it has no heap provenance (**GpuCaptureOk-Const**).
- A **`unique`** binding may be captured iff it is explicitly moved, its type is `GpuSafeType`, and it has no heap provenance (**GpuCaptureOk-Unique-Move**).
- Capturing a **`shared`** binding into a GPU context is rejected (**GpuCapture-Shared-Err**, `E-CON-0151`) — the key system does not exist on the GPU.
- Capturing any binding with **heap provenance** is rejected (**GpuCapture-HeapProv-Err**), and capturing any **non-GPU-safe** type is rejected (**GpuCapture-NonGpuSafe-Err**, `E-CON-0153`).

`HasHeapProvenance(Γ, x)` is transitive through derivation: a value derived from a heap-backed value (`π_Derived`) is itself heap-provenanced. Heap provenance (`π_Heap`) arises only from operations that explicitly take a `$HeapAllocator` capability.

#### 24.3.3 Capture example

```ultraviolet
/// `config` is captured by reference (const); `buffer` ownership moves into the task.
procedure transform(
    context: Context,
    config: const TransformConfig,
    buffer: move unique Buffer
) -> Spawned<Buffer> {
    let handle: Spawned<Buffer> = parallel context~>cpu() {
        spawn {
            applyTransform(config, move buffer)
        }
    }
    return handle
}
```

Here `config` (a `const` binding) is captured by reference, while `buffer` (a `unique` binding bound in the enclosing parallel body) is moved into the single child task with an explicit `move`, satisfying `FirstChildMove`.

#### 24.3.4 Diagnostics (§20.3.7)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0120` | Error | Implicit capture of `unique` binding (`Capture-Unique-Err`) |
| `E-CON-0121` | Error | Move of already-moved binding |
| `E-CON-0122` | Error | Move of binding from outer parallel scope (`Parallel-Closure-Capture-OuterMove-Err`) |
| `E-CON-0131` | Error | `spawn` in escaping closure |
| `E-CON-0151` | Error | `shared` capture in GPU context |
| `E-CON-0153` | Error | Non-GpuSafe type in GPU capture |

### 24.4 Spawn, `Spawned<T>`, and Wait (§20.4, §21.2)

`spawn` forks one task and immediately hands back a typed handle; `wait` consumes that handle to retrieve the task's result.

#### 24.4.1 Spawn syntax (§20.4.1)

```ebnf
spawn_expr         ::= "spawn" spawn_option_list? block
spawn_option_list  ::= "[" spawn_option ("," spawn_option)* "]"
spawn_option       ::= "name" ":" string_literal
                     | "affinity" ":" expression
                     | "priority" ":" expression
```

`spawn` is a reserved keyword (in `BeginsOperand`). The option lexemes `name`, `affinity`, and `priority` are fixed identifiers (`FixedIdent_Spawn`). The body is a block.

#### 24.4.2 Spawn options and typing (§20.4.4)

```text
SpawnOptOk(Name(_))      ⇔ true
SpawnOptOk(Affinity(e))  ⇔ Γ ⊢ e : TypePath(["CpuSet"])
SpawnOptOk(Priority(e))  ⇔ Γ ⊢ e : TypePath(["Priority"])
```

The typing rule **(T-Spawn)** requires an enclosing `parallel_context`:

```text
Γ[parallel_context] = D    SpawnOptsOk(opts)    Γ_capture ⊢ e : T
─────────────────────────────────────────────────────────────────
Γ ⊢ `spawn` opts {e} : Spawned⟨T⟩
```

A `spawn` whose body has type `T` produces a value of type `Spawned<T>`. Using `spawn` with no enclosing parallel block is `E-CON-0101` (the diagnostic is owned by §20.1.7). A spawn option of the wrong type is `E-CON-0130`.

#### 24.4.3 The `Spawned<T>` modal (§13.1.4)

`Spawned` is a built-in modal type with two states:

```text
States(Spawned) = { @Pending, @Ready }
SpawnedParams      = [⟨T, [], ⊥, ⊥⟩]
Payload(Spawned, @Pending) = []
Payload(Spawned, @Ready)   = [⟨value, T⟩]
```

A fresh handle is `Spawned<T>@Pending`. When the task settles successfully, the handle holds `@Ready { value: T }`. (Failure by panic is *not* a third handle state visible to user code — it is consumed by the block's panic propagation; see §24.7.) `Spawned` is a reserved, universe-protected built-in modal and cannot be declared by user code.

#### 24.4.4 Spawn dynamic semantics (§20.4.5)

Evaluating `spawn [opts] { e }` (**EvalSigma-Spawn**) does the following:

1. Capture the free variables per §24.3 (`CapturedEnv(e, σ)`).
2. Package the captured environment and body into a work item with a fresh `id` (`NextWorkId(pstate)`).
3. If `affinity` is present, restrict worker selection to the CPU indices set in the `CpuSet` mask (an empty set falls back to the domain default).
4. If `priority` is present, assign that `Priority`; among ready tasks, workers MUST select one of maximal priority.
5. Enqueue the work item on the block's domain (`EnqueueWork`).
6. **Return `Spawned<T>@Pending` immediately** — `spawn` never blocks.

The handle is appended to the block's `Handles` list, which is exactly what the join (§24.1.4) waits on.

#### 24.4.5 Wait (§21.2)

`wait` is the consumer. It is a primary expression; `wait` is a *contextual* keyword (`CtxKeyword = {"in", "key", "wait", "new"}`), recognized in identifier position only when followed by an operand (`Parse-Wait-Expr` matches `IsIdent(Tok(P)) ∧ Lexeme = wait`).

```ebnf
wait_expr ::= "wait" expression
```

Typing **(T-Wait)** / **(T-Wait-Future)**:

```text
Γ; R; L ⊢ h : TypeApply(["Spawned"], [T])
──────────────────────────────────────────
Γ; R; L ⊢ `wait` h : T

Γ; R; L ⊢ h : TypeApply(["Tracked"], [T, E])
──────────────────────────────────────────
Γ; R; L ⊢ `wait` h : TypeUnion([T, E])
```

So `wait` on a `Spawned<T>` yields `T`, and `wait` on a `Tracked<T, E>` (the async result handle of *Chapter 21*) yields `T | E`. Applying `wait` to anything whose stripped-permission type is neither `Spawned<_>` nor `Tracked<_, _>` is rejected (`Wait-Handle-Err`, `E-CON-0132`).

`wait` evaluation (**EvalSigma-Wait-Spawned-Ready / -Pending**):

1. Evaluate `h`.
2. If the handle is `@Ready`, return its `value`.
3. If the handle is `@Pending`, block the current task until it settles (`BlockUntilSettledSpawned`), then return the value.
4. If a `Spawned<T>` handle settles by panic, that failure is **not** observed by `wait`; it is consumed by the enclosing block's panic propagation (§24.7).

**Key restriction (§21.5.4).** `wait` is ill-formed at any program point where a key is held (`Γ_keys(p) ≠ ∅`) — raising `E-CON-0133`. Retrieve results *after* leaving the key block, or restructure with the `yield release` forms of *Chapter 21*. This prevents a task from blocking on a result while pinning keys that the awaited task might need.

#### 24.4.6 Spawn/wait example

```ultraviolet
/// Fork a labeled, high-priority probe and a default-priority bulk task, then join both.
procedure scanRegion(context: Context, region_id: RegionId, data: const Dataset) -> ScanResult {
    let result: ScanResult = parallel context~>cpu() [name: "scan-region"] {
        let probe: Spawned<Probe> = spawn [name: "probe", priority: Priority::High] {
            quickProbe(region_id, data)
        }
        let bulk: Spawned<Histogram> = spawn [name: "bulk"] {
            buildHistogram(data)
        }

        ScanResult {
            probe: wait probe,
            histogram: wait bulk,
        }
    }
    return result
}
```

#### 24.4.7 Diagnostics (§20.4.7, §21.2.7, §21.5.7)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0130` | Error | Invalid spawn option type |
| `E-CON-0132` | Error | `wait` operand is not `Spawned` or `Tracked` (`Wait-Handle-Err`) |
| `E-CON-0133` | Error | `wait` while a key is held |

### 24.5 Dispatch — Parallel Loops (§20.5)

`dispatch` runs a loop body in parallel across a range, automatically partitioning iterations so that conflicting accesses are serialized and independent iterations run concurrently. It optionally reduces results to a single value.

#### 24.5.1 Syntax (§20.5.1)

```ebnf
dispatch_expr         ::= "dispatch" pattern "in" range_expression key_clause? dispatch_option_list? block
key_clause            ::= "key" key_path_expr key_mode
key_mode              ::= "read" | "write"
dispatch_option_list  ::= "[" dispatch_option ("," dispatch_option)* "]"
dispatch_option       ::= "reduce" ":" reduce_op
                        | "ordered"
                        | "chunk" ":" expression
                        | "workgroup" ":" dim3_const
reduce_op             ::= "+" | "*" | "min" | "max" | "and" | "or" | identifier
```

`dispatch` is a reserved keyword (in `BeginsOperand`); `in`, `key`, `wait`, and `new` are contextual keywords (`CtxKeyword`); `reduce`, `ordered`, `chunk`, `workgroup`, `min`, `max`, `and`, and `or` are fixed identifiers (`FixedIdent_Dispatch`), tokenized as identifiers and accepted in dispatch position. The `range_expression` parses as a non-brace expression (`ParseExpr_NoBrace`). The loop pattern binds the iteration index.

#### 24.5.2 Typing (§20.5.4)

A dispatch requires an enclosing `parallel_context`. The base CPU typing rules:

```text
Γ ⊢ range : Range<I>    Γ, i : I ⊢ B : T
──────────────────────────────────────────
Γ ⊢ `dispatch` i `in range` {B} : ()

Γ ⊢ range : Range<I>    Γ, i : I ⊢ B : T    Γ ⊢ op : (T, T) -> T
─────────────────────────────────────────────────────────────────
Γ ⊢ `dispatch` i `in range [reduce: op]` {B} : T
```

Without `reduce`, a dispatch has type `()` and discards iteration results. With `reduce: op`, the body's result type `T` must support `op : (T, T) -> T`, and the dispatch evaluates to the reduced `T`. The GPU variants (**T-GPU-Dispatch** / **T-GPU-Dispatch-Reduce**) additionally require `GpuContext(Γ)`, a valid `ComputeTopologyDispatch` topology, `GpuSafeType(T)` for the reduced form, and `GpuCaptureOk` for every free variable of the body. Dispatch outside any parallel block raises `Dispatch-Outside-Err` (`E-CON-0140`).

#### 24.5.3 Dependency safety and key inference (§20.5.4)

`dispatch` is safe by construction: iterations that might touch the same data are placed in the same partition group and serialized; only provably independent iterations run concurrently.

- **Explicit `key` clause.** `key <path> read` or `key <path> write` states the access summary directly. The partition spec is `[⟨SchemaOf(pat, key_e), mode⟩]`.
- **Inferred access.** With no `key` clause, the compiler MUST infer accesses with `InferDispatchAccesses(pat, B)`: it scans the body for `shared`-typed uses outside key blocks (`ImplicitDispatchUse`), derives each access path schema (`SchemaOf`), and joins read/write modes per schema (`MergeDispatchAccesses`). The fundamental guarantee for an indexed write is:

  ```text
  `dispatch` v `in` r { … a[v] … }
  ────────────────────────────────────────────
  ∀ v_1, v_2 ∈ r, v_1 ≠ v_2 ⇒ ProvablyDisjoint(a[v_1], a[v_2])
  ```

  Disjointness is decided syntactically (`ProvablyDisjoint`): distinct integer literals are disjoint, and two *affine* index expressions `⟨x, k₁⟩`, `⟨x, k₂⟩` over the same base are disjoint when `k₁ ≠ k₂` (so `a[v]` and `a[v + 1]` never collide across iterations).

If the inferred accesses contain two schemas that are neither provably disjoint nor mode-compatible, the dispatch is rejected with `Dispatch-Dependency-Err` (`E-CON-0142`). If key inference cannot summarize an access at all, `E-CON-0141` requires an explicit `key` clause. If an index pattern is dynamic (not a `DispatchStaticIndexExpr`), the compiler emits the warning `Dispatch-DynamicKey-Warn` (`W-CON-0140`): the dispatch still runs, but conflicting dynamic keys serialize at runtime.

#### 24.5.4 Reduction, ordering, and chunking (§20.5.4)

- **`reduce: op`.** The built-in operators `+`, `*`, `min`, `max`, `and`, `or` are *associative* (`AssociativeReduce`). A user-named `identifier` reducer is **not** treated as associative. A non-associative reduction without `[ordered]` is rejected (`Dispatch-Reduce-Assoc-Err`, `E-CON-0143`) because the result would be non-deterministic. Reducing over an **empty** iteration space is a runtime panic `P-SEM-2862` (§20.9).
- **`ordered`.** Forces sequential side-effect ordering: side effects within each group are buffered and committed in ascending iteration order after the group completes (§20.8.5). `ordered` also licenses a non-associative reducer.
- **`chunk: e`.** `e` is evaluated exactly once before partitioning and MUST have type `usize` (else `Dispatch-Chunk-Type-Err`). The positive result partitions each post-key group into contiguous chunks of at most that size, reducing per-task scheduling overhead.
- **`workgroup: (x, y, z)`.** GPU topology override, as in §24.1.2.

#### 24.5.5 Dispatch dynamic semantics (§20.5.5)

Evaluating a dispatch (**EvalSigma-Dispatch**): evaluate the range; evaluate `chunk` if present (`ChunkSizeOf`, defaulting to 1); build the partition with `DispatchPartition` (group by key conflicts via connected components, ordered by least member); split groups into contiguous chunks (`ChunkGroups`); then `DispatchRun` executes all groups. Without a reducer the result is `()`; with a reducer it is the deterministic reduction of all iteration results. If any iteration panics, all started iterations settle and the dispatch yields a panic (consumed per §24.7). A control effect while evaluating the range or the chunk expression short-circuits (**EvalSigma-Dispatch-Range-Ctrl**, **EvalSigma-Dispatch-Chunk-Ctrl**).

#### 24.5.6 Dispatch examples

```ultraviolet
/// Element-wise parallel write: each iteration touches a distinct index, so all run concurrently.
procedure normalizeInPlace(context: Context, values: shared [f32], scale: const f32) -> () {
    parallel context~>cpu() {
        dispatch i in 0..values~>len() {
            values[i] = values[i] * scale
        }
    }
}
```

```ultraviolet
/// Parallel sum reduction with explicit chunking; `+` is associative, so no `ordered` is needed.
procedure parallelSum(context: Context, samples: const [i64]) -> i64 {
    let total: i64 = parallel context~>cpu() {
        dispatch i in 0..samples~>len() [reduce: +, chunk: 1024usize] {
            samples[i]
        }
    }
    return total
}
```

```ultraviolet
/// Accumulate into per-account buckets keyed by account id; the key clause serializes
/// only iterations that target the same account, leaving distinct accounts concurrent.
procedure postLedger(context: Context, entries: const [Entry], ledger: shared Ledger) -> () {
    parallel context~>cpu() {
        dispatch e in 0..entries~>len() key ledger.accounts[entries[e].account] write {
            applyEntry(ledger, entries[e])
        }
    }
}
```

#### 24.5.7 Diagnostics (§20.5.7, §20.9)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0140` | Error | Dispatch outside parallel block (`Dispatch-Outside-Err`) |
| `E-CON-0141` | Error | Key inference failed; explicit `key` required |
| `E-CON-0142` | Error | Cross-iteration dependency detected (`Dispatch-Dependency-Err`) |
| `E-CON-0143` | Error | Non-associative reduction without `[ordered]` (`Dispatch-Reduce-Assoc-Err`) |
| `W-CON-0140` | Warning | Dynamic key pattern; runtime serialization (`Dispatch-DynamicKey-Warn`) |
| `P-SEM-2862` | Panic (runtime) | Reduced dispatch over empty iteration space |

### 24.6 Cancellation (§20.6)

Cancellation in Ultraviolet is **cooperative** and **structured**: a `CancelToken` is attached to a `parallel` block, propagated implicitly to its tasks, and checked at well-defined points.

#### 24.6.1 The `CancelToken` modal (§20.6.3, §20.6.4)

`CancelToken` is a built-in modal type with a single state:

```text
States(CancelToken)  = { @Active }
Payload(CancelToken, @Active) = [⟨id, usize⟩]
```

Its methods (§20.6.4):

- `CancelToken::new()` → `CancelToken@Active` — create a fresh root token.
- `CancelToken@Active::child()` → a descendant `CancelToken@Active` — create a child token; cancelling a token cancels it and all descendants.
- `CancelToken@Active::cancel()` → `()` — request cancellation of this token and its descendants.
- `CancelToken@Active::is_cancelled()` → `bool` — a cheap synchronous check point.
- `CancelToken@Active::wait_cancelled()` → an `Async` value whose eventual completion indicates cancellation — an awaitable check point.

`CancelToken` is reserved and universe-protected.

#### 24.6.2 Attaching and propagating (§20.6.5)

A token is attached to a block by the `cancel:` option (§24.1.2): `parallel D [cancel: token] { … }`. When attached, the token is **implicitly available within all enclosed `spawn` and `dispatch` bodies** — tasks query it without it being captured explicitly. Cancellation is hierarchical: `cancel()` marks the token's whole descendant subtree `Cancelled` (**Cancel-DoCancel** over `Descendant`).

Cancellation is cooperative, with these guarantees (§20.6.5):

| Scenario | Behavior |
| --- | --- |
| Work checks and returns early | Iteration completes immediately |
| Work ignores cancellation | Iteration runs to completion |
| Work is queued but not started | MUST be dequeued, marked cancelled, and not executed |
| Work is mid-execution | Continues until its next check point |

So cancellation never preempts running code; tasks that never check `is_cancelled()`/`wait_cancelled()` run to completion. The one guarantee that does not depend on the body cooperating: queued-but-unstarted work MUST be dropped without executing (lowered through `CancelSuppressIR`).

#### 24.6.3 Cancellation example

```ultraviolet
/// Race many candidate searches; the first to find a hit cancels the rest.
procedure findFirst(context: Context, shards: const [Shard], needle: const Key) -> Match {
    let token: CancelToken = CancelToken::new()
    let best: shared MatchSlot = MatchSlot::empty()

    parallel context~>cpu() [cancel: token, name: "find-first"] {
        dispatch s in 0..shards~>len() {
            if token~>is_cancelled()
                continue

            if searchShard(shards[s], needle) is Match::Found { hit } {
                recordMatch(best, hit)
                token~>cancel()
            }
        }
    }

    return takeMatch(best)
}
```

Each iteration polls `is_cancelled()` and returns early once any shard has signalled a hit; unstarted shard iterations are suppressed.

#### 24.6.4 Diagnostics (§20.6.7)

No diagnostics are specific to cancellation. (The `cancel:` option's type requirement is enforced by §24.1's `Parallel-Domain-Param-Err`/`E-CON-0102`.)

### 24.7 Panic Handling Across Tasks (§20.7)

A panic in one task does not corrupt the structured scope. Panics are captured, the block still joins, and exactly one panic re-emerges at the block boundary.

#### 24.7.1 Semantics (§20.7.5)

When a work item panics within a parallel block:

1. The panic is captured (the handle settles `Failed`).
2. Other work items continue to completion — *or*, if a cancel token is attached, cancellation is requested.
3. After all started work settles, a panic is propagated at the block boundary.

The propagation rule **(EvalSigma-Parallel-Spawn-Panic)** runs the join, observes a non-`⊥` `panic_opt` from `AwaitSpawned`, and yields `Ctrl(Panic)` for the whole block. The block never returns its normal value when a task has failed.

**Which panic?** When several tasks fail, the block re-emits the failure with the **least `CompletionSeq`** — the earliest task to settle as failed (`FirstCompletedFailure`; `AwaitSpawned` returns "the failure associated with the least completion-sequence number among handles whose terminal state is `Failed`"). This makes multi-failure behavior deterministic.

**Cancel interaction.** If a cancel token is attached, the runtime MUST request cancellation on the first captured panic, **exactly once**. If no cancel token is attached, a panic alone MUST NOT request cancellation — sibling tasks run to completion. This is the structured-concurrency contract: a failing task can abort its siblings *only* when the programmer opted in with a cancel token.

Note the division of labor with `wait` (§24.4.5): `wait` only ever observes a *successful* `@Ready` value; a failed `Spawned<T>` is consumed by this block-level propagation, never surfaced through `wait` as a value.

#### 24.7.2 Panic-handling example

```ultraviolet
/// If any validator panics, the cancel token aborts the remaining validators,
/// and the earliest failure re-emerges at the block boundary.
procedure validateAll(context: Context, docs: const [Document]) -> ValidationReport {
    let token: CancelToken = CancelToken::new()

    let report: ValidationReport = parallel context~>cpu() [cancel: token] {
        let structural: Spawned<StructuralOk> = spawn { validateStructure(docs) }
        let semantic:   Spawned<SemanticOk>   = spawn { validateSemantics(docs) }

        ValidationReport {
            structure: wait structural,
            semantics: wait semantic,
        }
    }

    return report
}
```

If `validateStructure` panics, the attached token causes the runtime to request cancellation of `validateSemantics` exactly once; once both have settled, the structural panic (least `CompletionSeq`) propagates out of `validateAll`.

#### 24.7.3 Diagnostics (§20.7.7)

No named diagnostics are introduced for panic propagation; it is a runtime mechanism.

### 24.8 Determinism and Nesting (§20.8)

Structured parallelism is designed so that, under stated conditions, parallel results match a well-defined sequential schedule.

#### 24.8.1 Determinism (§20.8.4, §20.8.5)

**Dispatch is deterministic when** (§20.8.4):

1. key patterns produce identical partitioning across runs;
2. iterations with the same key execute in index order;
3. reduction uses deterministic tree combination.

The `[ordered]` option additionally forces sequential side-effect ordering (buffer-then-commit in ascending iteration order). The scheduling rules that underpin determinism (§20.8.5):

- Within each CPU domain queue, ready work items are dequeued in ascending `TaskId` (the stable creation identifier `TaskId(w)`).
- Dispatch groups are scheduled in ascending order of their least iteration index.
- The inline domain executes work immediately at creation — a strict sequential schedule.
- GPU work-items execute in lexicographic order of `(workgroup_id, local_id)` for the abstract semantics.
- `CompletionSeq(w)` assigns a global monotonically increasing completion id, which (per §24.7) selects the propagated panic deterministically.

Because associative reducers combine via a deterministic tree, `reduce: +` (and the other built-ins) produce the same result regardless of how iterations were partitioned across workers. A user-named reducer must be marked `ordered` precisely because the compiler cannot assume it associates.

#### 24.8.2 Nesting (§20.8.4, §20.8.5)

- **GPU-in-GPU is forbidden.** GPU parallel blocks MUST NOT be nested inside other GPU parallel blocks (`E-CON-0152`). Nested GPU-in-GPU lowering is ill-formed and emits no IR.
- **CPU-in-CPU shares the pool.** Inner CPU parallel blocks share the worker pool with outer CPU parallel blocks (nested CPU lowering reuses the enclosing pool handle), so deep nesting does not multiply thread pools.
- **Heterogeneous nesting is allowed.** CPU and GPU blocks MAY be nested heterogeneously (e.g. a GPU block inside a CPU block).
- **Captures re-checked per level.** Capture rules apply independently at each nesting level — an inner block re-validates its own captures.
- **Allocation in tasks.** Work items MAY capture the `context.heap` field (a `$HeapAllocator` capability) and invoke allocation methods; a work item executing within a `region` block MAY allocate from that region using `new value`, or using a `Region@Active` handle's `~>alloc` operation when it must name an explicit target.

#### 24.8.3 Nesting example

```ultraviolet
/// Outer CPU fan-out over tiles; each tile runs an inner CPU dispatch.
/// Inner blocks reuse the outer worker pool.
procedure shadeTiles(context: Context, tiles: shared [Tile]) -> () {
    parallel context~>cpu() [name: "outer"] {
        dispatch t in 0..tiles~>len() {
            parallel context~>cpu() [name: "inner"] {
                dispatch p in 0..tiles[t]~>pixelCount() {
                    shadePixel(tiles[t], p)
                }
            }
        }
    }
}
```

#### 24.8.4 Diagnostics (§20.8.7)

| Code | Severity | Condition |
| --- | --- | --- |
| `E-CON-0152` | Error | Nested GPU parallel block |

### 24.9 Worked Structured-Concurrency Example

The following procedure ties the chapter together: a domain bound to a fixed core set, a cancellation token, `spawn`/`wait` for heterogeneous tasks, and structured panic propagation. It pins a build pipeline to a CPU mask, runs three independent stages, cancels the rest if any stage fails, and joins.

```ultraviolet
/// Build an asset bundle on a pinned CPU domain.
///
/// Forks three independent stages under one cancel token. If any stage panics,
/// the token aborts the siblings exactly once and the earliest failure
/// propagates out of `buildBundle`. The block does not return until every
/// forked stage has settled.
procedure buildBundle(
    context: Context,
    sources: const SourceSet,
    cores: CpuSet
) -> Bundle {
    let domain: $ExecutionDomain = context~>cpu(cores, Priority::Normal)
    let token: CancelToken = CancelToken::new()

    let bundle: Bundle = parallel domain [cancel: token, name: "build-bundle"] {
        let meshes: Spawned<MeshTable> = spawn [name: "meshes", priority: Priority::High] {
            if token~>is_cancelled()
                return MeshTable::empty()
            cookMeshes(sources)
        }

        let textures: Spawned<TextureTable> = spawn [name: "textures"] {
            if token~>is_cancelled()
                return TextureTable::empty()
            cookTextures(sources)
        }

        let shaders: Spawned<ShaderTable> = spawn [name: "shaders"] {
            if token~>is_cancelled()
                return ShaderTable::empty()
            cookShaders(sources)
        }

        Bundle {
            meshes: wait meshes,
            textures: wait textures,
            shaders: wait shaders,
        }
    }

    return bundle
}
```

What this guarantees:

- **Fork-join (§24.1):** `buildBundle` cannot return while any of `meshes`, `textures`, or `shaders` is still running; the join at the block boundary blocks on all three even though each is also `wait`-ed explicitly.
- **Domain (§24.2):** all three stages run on the same pinned CPU domain, restricted to `cores`.
- **Capture (§24.3):** `sources` is a `const` binding captured by reference into every stage; `token` is implicitly available to all stages because it was attached via `cancel:`.
- **Cancellation + panic (§24.6–§24.7):** because a `cancel` token is attached, the first stage to panic triggers cancellation of the siblings exactly once; the earliest failure (least `CompletionSeq`) is the panic that leaves `buildBundle`.
- **Determinism (§24.8):** stages are dequeued in ascending `TaskId`, and `wait` retrieves each settled value.

### Idioms & Best Practices

- **Let the block own the lifetime.** Prefer forking inside a `parallel` block and letting the implicit join collect tasks over manually tracking handles. The structured boundary is the feature; do not try to defeat it. A `spawn` may not appear in an escaping closure (`E-CON-0131`) precisely to keep tasks from outliving their block.
- **Name your blocks and long-lived tasks.** Use `name: "..."` on `parallel` blocks and on significant `spawn` tasks. Names surface in diagnostics and tooling at no runtime cost, matching the style guide's preference for legibility during review.
- **Pass narrow capabilities, not god-contexts.** A task body should capture only the `const`/`shared` data it actually uses. This aligns with the style guide's *Capability Passing* rule: pass only the exact capabilities a procedure uses, and do not thread broad context bundles through parallel bodies.
- **Prefer `dispatch` over hand-rolled `spawn` loops** for data-parallel work. `dispatch` infers the access summary, proves iteration independence, and partitions for you; a loop of `spawn`s gives up that analysis and the per-key serialization guarantee.
- **Reduce with the built-ins when you can.** `reduce: +`, `*`, `min`, `max`, `and`, `or` are associative and deterministic without extra annotations. Reserve a named reducer (which requires `[ordered]`) for genuinely order-sensitive folds.
- **Make cancellation cooperative and explicit.** Attach a `cancel:` token when you want sibling-abort-on-failure or early termination, and poll `is_cancelled()` at natural boundaries inside long iterations. Without a token, a panic will *not* stop siblings — that is by design.
- **Use the inline domain for tests and determinism.** `context~>inline()` runs the identical program sequentially with all capture and permission rules enforced (§20.2.5), which makes parallel logic reproducible under test before scaling to `cpu()`/`gpu()`.
- **Keep GPU bodies `GpuSafe` and heap-free.** Restrict GPU captures to `Bitcopy`, non-heap, GPU-safe data; remember the key system and managed `string`/`bytes` are unavailable on the GPU.

### Pitfalls & Diagnostics

- **`spawn`/`dispatch` outside a `parallel` block.** Both are legal only where `parallel_context` is bound. A stray `spawn` is `E-CON-0101`; a stray `dispatch` is `E-CON-0140` (`Dispatch-Outside-Err`).
- **Capturing a `unique` binding by reference.** Implicit `unique` capture is `E-CON-0120`. You may *move* a `unique` binding into exactly one child task (with explicit `move`, when it is bound in the enclosing parallel body); moving it into a second task is `E-CON-0122`, and re-moving an already-moved binding is `E-CON-0121`.
- **`wait` while holding a key.** `wait` is forbidden at any point where a key is held (`E-CON-0133`). Restructure so the `wait` occurs outside the key block, or use the `yield release` async forms of *Chapter 21*. Likewise, `wait` only accepts `Spawned<T>` or `Tracked<T, E>` operands (`E-CON-0132`).
- **Non-associative reduction without `[ordered]`.** A user-named `reduce:` operator without `[ordered]` is `E-CON-0143` (`Dispatch-Reduce-Assoc-Err`). Add `[ordered]` or use an associative built-in.
- **Cross-iteration dependency in `dispatch`.** If the access analysis cannot prove iterations independent, you get `E-CON-0142` (`Dispatch-Dependency-Err`); if it cannot summarize the access at all, `E-CON-0141` demands an explicit `key` clause. A dynamic (non-static) index pattern downgrades to the warning `W-CON-0140` and serializes conflicting keys at runtime — usually a signal to restructure the index or supply a `key` clause.
- **Reduced dispatch over an empty range.** `dispatch i in 0..0 [reduce: +] { … }` panics at runtime with `P-SEM-2862` — there is no identity element to return. Guard empty ranges before reducing.
- **Bad domain or option types.** A domain expression that does not type as `$ExecutionDomain`, or a `cancel:` operand that is not a `CancelToken`, raises `E-CON-0102`; a malformed `dim3_const` (a zero or non-`usize` component) raises `E-CON-0103` (`E-CON-0159` for a GPU topology option). A `chunk:` expression that is not `usize` is rejected by `Dispatch-Chunk-Type-Err`.
- **Wrong call operator.** `cpu`/`gpu`/`inline` are *methods*, so they are called with `~>` (`context~>cpu()`); writing `context.cpu()` is field access and is wrong. Conversely, `heap` is a *field*, so `context.heap` is correct and `context~>heap` is wrong.
- **GPU-specific traps.** Nesting a GPU block in a GPU block is `E-CON-0152`. Using a key block, a `shared` capture, a non-`GpuSafe` type, a heap-provenanced value, a barrier under divergent control flow, or a workgroup exceeding 1024 work-items inside GPU code each has its own error (`E-CON-0155`, `E-CON-0151`, `E-TYP-2640`/`E-CON-0153`, `E-CON-0156`/`E-CON-0158`, `E-CON-0157`). Calling a GPU intrinsic outside a GPU context is `E-CON-0154` (`E-CON-0156` for barriers).
- **Assuming a panic stops siblings.** Without a `cancel` token, sibling tasks run to completion even after another task panics; the block still propagates the earliest panic at its boundary. Attach a token if you need abort-on-failure.

Related chapters: *Chapter 13, Modal Types* (`Spawned<T>`, `Tracked<T, E>`, `CancelToken` as built-in modals); *Chapter 16, Closures* (capture-set computation reused here); *Chapter 19, The Key System* (`shared` synchronization, `key` clauses, key-held restrictions on `wait`); *Chapter 21, Asynchronous Execution* (`wait`, `Tracked<T, E>`, `yield`/`yield release`); *Chapter 22, Regions* (`new` and `Region@Active~>alloc` inside work items).
