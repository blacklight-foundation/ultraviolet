## 19. Statements, Blocks, Regions, Frames & Defer

This chapter specifies the body of every Ultraviolet procedure, method, transition, and block expression: the statements that compose a block, how a block produces a value, the binding forms `let` and `var`, local `using` aliases, assignment and compound assignment, expression statements, deferred cleanup with `defer`, arena scopes with `region`, stack-like scopes with `frame`, control transfer with `return` / `break` / `continue`, and `unsafe` statements. It corresponds to specification §18 (Statements and Blocks). Region allocation uses `new` for the current scoped region and `Region@Active~>alloc` for an explicit target; this chapter shows how both forms are used inside `region` and `frame` scopes.

Related material: blocks are also expressions (Expressions chapter, §16.7); patterns used in `let` / `var` are covered in the Patterns chapter (§17); `loop`, `if`, and case analysis are in the Control-Flow Expressions chapter (§16.7); the `key` system that mediates `shared` writes is in the Key System chapter (§19 of the spec); permission semantics (`const`, `unique`, `shared`) are in the Permissions chapter (§10.4).

> Terminology note: Ultraviolet does **not** have loop labels. `break` and `continue` always target the innermost enclosing `loop`. There is no `break 'name` / `continue 'name` syntax, and the grammar admits no label production anywhere. "Labels" in the colloquial sense are therefore covered here by the single rule that control transfer targets the nearest `loop`.

> Brace note for examples: every `if`, `loop`, and `else` body in this chapter uses an explicit brace-delimited `block_expr`. The grammar (`if_tail`, `loop_expr`, §16.7.1) parses these bodies through `ParseBlock`, which requires an opening `{` (§18.1.2, `Parse-Block`). The examples here keep braces on every control-flow body so they parse as written. (See Pitfalls for the relationship to the style guide's single-statement allowance.)

### 19.1 Blocks and Block Values (§18.1)

A **block** is a brace-delimited sequence of statements followed by an optional trailing tail expression. The block *is* an expression: its value and type come from the tail.

#### 19.1.1 Grammar

```ebnf
statement_seq ::= statement* expression?
statement     ::= binding_stmt
                | using_local_stmt
                | assignment_stmt
                | compound_assign
                | expr_stmt
                | defer_stmt
                | region_stmt
                | frame_stmt
                | return_stmt
                | break_stmt
                | continue_stmt
                | unsafe_block
                | key_block_stmt
                | comptime_stmt
block_expr    ::= "{" statement_seq "}"
```

The consolidated grammar (Appendix B.5) lists the same statement forms:

```ebnf
statement ::= binding_stmt | using_local_stmt | assignment_stmt | compound_assign
            | expr_stmt | return_stmt | break_stmt | continue_stmt | defer_stmt
            | region_stmt | frame_stmt | unsafe_block | key_block_stmt | comptime_stmt
```

`key_block_stmt` is defined in the Key System chapter (Chapter 19 of the spec); `comptime_stmt` is defined in the Metaprogramming chapter (§22.1.1). A statement may carry a leading attribute list (Chapter 9); attributes are attached by `AttachStmtAttrs` during parsing.

#### 19.1.2 Statement terminators

A statement is terminated by a newline or a semicolon. The terminator set and grammar are:

```text
StmtTerm = {Punctuator(";"), Newline}
```

```ebnf
terminator ::= ";" | newline
newline    ::= "\n"
```

Newlines are the default terminator. Use `;` only to place several short statements on one line, or where surrounding syntax requires it (the style guide prefers newline termination; see §19.11).

The following statement forms **require** a terminator (`ReqTerm`):

- `let` / `var` bindings (`LetStmt`, `VarStmt`)
- local `using` (`UsingLocalStmt`)
- assignment and compound assignment (`AssignStmt`, `CompoundAssignStmt`)
- expression statements (`ExprStmt`)

The block-structured statements — `defer`, `region`, `frame`, `unsafe`, key blocks, and the control-flow expressions that end in a block — do not require a trailing terminator because their closing brace already delimits them. `return`, `break`, and `continue` take an **optional** terminator (`terminator?` in §18.9.1).

#### 19.1.3 The block value (tail expression)

A block's type is determined by how it ends. The spec partitions blocks into exactly three `BlockInfo` cases:

- **Tail value** (`BlockInfo-Tail`): if the block ends with a tail expression `e : T` (and `e` is not a return-shaped tail), the block has type `T` and evaluates to the value of `e`.
- **Unit** (`BlockInfo-Unit`): if there is no tail expression and the last statement is not a `return`, the block has type `()` (unit) and evaluates to `()`.
- **Return tail / never** (`BlockInfo-ReturnTail`): if there is no tail expression and the last statement is a `return`, the block has type `!` (never), because control leaves the block before any value is produced.

A tail expression has **no terminator** — it is the bare trailing expression. If you place a terminator after the last expression, it becomes an *expression statement*, the block has no tail, and the block's value is `()`.

```ultraviolet
// Block whose value is the tail expression: type i32, value 30.
let total: i32 = {
    let base: i32 = 10
    let bonus: i32 = 20
    base + bonus          // tail expression — no terminator
}

// Block with no tail: its value is unit.
let nothing: () = {
    let scratch: i32 = 99
    logScratch(scratch)   // expression statement, terminated by newline
}
```

The `let total` block ends in `base + bonus` with no terminator, so the block's type is `i32`. The `let nothing` block ends in a terminated statement, so its type is `()`.

#### 19.1.4 Scope, evaluation order, and cleanup

Entering a block pushes a fresh lexical scope (`PushScope` / `BlockEnter`); statements execute top to bottom (`ExecSeqSigma`); the tail expression, if present, is evaluated last. Leaving the block runs scope cleanup (`BlockExit` → `CleanupScope`) — this is where `defer` blocks and value destructors run (see §19.6) — then pops the scope.

If any statement produces a control transfer (`Ctrl(κ)` — a `Return`, `Break`, `Continue`, `Panic`, or `Abort`), the remaining statements and the tail are skipped (`ExecSeq-Cons-Ctrl`), but scope cleanup still runs on the way out (`BlockExit`).

A block-prefix statement sequence whose result set has no common type is a block-result join failure (`BlockInfo-Res-Err`, §18.1.7). Unreachable block results are reported by `WarnResultUnreachable`.

### 19.2 Binding Statements: `let` and `var` (§18.2)

#### 19.2.1 Grammar

```ebnf
binding_stmt ::= ("let" | "var") pattern (":" type)? binding_op expression terminator
binding_op   ::= "=" | ":="
```

`let` introduces an **immutable** binding (`MutKind` = `let`). `var` introduces a **mutable** binding (`MutKind` = `var`). The left side is a *pattern* (Patterns chapter, §17), so bindings may destructure tuples, records, and enum/modal payloads. An optional `: type` annotation constrains the binding; without it the type is inferred from the initializer.

#### 19.2.2 The two binding operators: `=` vs `:=`

The binding operator chooses the **movability** of the binding:

```text
Movability ::= mov | immov
MovOf("=")  = mov      // ordinary binding
MovOf(":=") = immov    // immovable (owning) binding
```

- `=` produces an ordinary (`mov`) binding. The initializer's value is bound; the binding may be moved out of later according to the normal ownership rules.
- `:=` produces an **immovable** (`immov`) binding: the binding owns its value and may not be moved out of. Attempting to move from an immovable `:=` binding is `E-MEM-3006`. Use `:=` when a value must stay put for the lifetime of its scope — for example a resource whose destructor must run in place and which must not escape.

```ultraviolet
let frame_count: u32 = 0          // immutable, ordinary
var cursor: usize = 0             // mutable, ordinary
let device := openDevice(handle)  // immutable, immovable: device stays in this scope
```

#### 19.2.3 Typing rules

For an annotated binding the initializer is checked against the annotation; for an unannotated binding the type is inferred and the pattern checked against it:

- `T-LetStmt-Ann`: with annotation `T_a`, check `init ⇐ T_a`, check the pattern against `T_a`, require `Distinct(PatNames(pat))`, then introduce the names as `let` bindings via `IntroAll`.
- `T-LetStmt-Infer`: with no annotation, infer `init ⇒ T_i`, solve, check the pattern against the solved type, then introduce the names.
- `T-VarStmt-Ann` / `T-VarStmt-Infer` are identical except the names are introduced as `var` bindings via `IntroAllVar`.

A binding pattern must introduce **distinct** names (`Distinct(PatNames(pat))`); a duplicate name within one pattern is a diagnostic.

The pattern in a `let` / `var` binding must be **irrefutable**. Literal, enum, modal, and range patterns are refutable and are rejected here (`Let-Refutable-Pattern-Err`); use `if ... is` or a `loop ... in` for refutable matching (Control-Flow and Patterns chapters).

A binding whose target type has `unique` permission and whose initializer is a place expression requires an explicit `move` (`B-LetVar-UniqueNonMove-Err`, `E-MEM-3007`):

```ultraviolet
let owned: unique Buffer = move source_buffer   // explicit move required
```

#### 19.2.4 Worked example

```ultraviolet
procedure summarize(samples: [f64]) -> f64 {
    var running_total: f64 = 0.0
    let sample_count: usize = samples.len()

    loop sample in samples {
        running_total += sample
    }

    if sample_count == 0 {
        return 0.0
    }

    return running_total / (sample_count as f64)
}
```

`running_total` is `var` because it is reassigned; `sample_count` is `let` because it is fixed after binding.

### 19.3 Local `using` Statements (§18.3)

#### 19.3.1 Grammar

```ebnf
using_local_stmt ::= "using" identifier "as" identifier terminator
```

A local `using` introduces an **alias** in the current block scope: after `using source as alias`, the name `alias` resolves to exactly the same entity as `source`. It introduces no new storage — `alias` and `source` denote the identical `Entity`, and aliasing an alias resolves through to the original.

#### 19.3.2 Semantics

- Static: `T-UsingLocalStmt` extends the environment through the `UsingAlias` judgment (§7.2). An unresolved source name, a reserved alias name, or an alias already bound in scope is a diagnostic (`T-UsingLocalStmt-Err`; see §7.2 `Using-Alias-*` rules).
- Dynamic: `ExecSigma-UsingLocal` has **no runtime effect** — name resolution is compile-time only.
- Lowering: `Lower-Stmt-UsingLocal` produces `NoOpIR`; the statement is consumed entirely during resolution.

#### 19.3.3 Worked example

```ultraviolet
procedure renderPass(frame_graph: FrameGraph) -> RenderReport {
    using frame_graph as graph

    let pass_count: usize = graph.passCount()
    return RenderReport { passes: pass_count }
}
```

Use a local `using` only where an alias genuinely improves clarity or resolves a real collision. Do not use it to simulate shadowing or to rename for cosmetic reasons (style guide).

### 19.4 Assignment Statements (§18.4)

#### 19.4.1 Grammar

```ebnf
assignment_stmt ::= place_expr "=" expression terminator
compound_assign ::= place_expr compound_op expression terminator
compound_op     ::= "+=" | "-=" | "*=" | "/=" | "%="
place_expr      ::= identifier | postfix_expr "." identifier | postfix_expr "[" expression "]"
```

The target of an assignment must be a **place expression** (an l-value). The place root is the underlying variable, computed by `PlaceRoot`:

```text
PlaceRoot(Identifier(x))      = x
PlaceRoot(FieldAccess(p, _))  = PlaceRoot(p)
PlaceRoot(TupleAccess(p, _))  = PlaceRoot(p)
PlaceRoot(IndexAccess(p, _))  = PlaceRoot(p)
PlaceRoot(Deref(p))           = PlaceRoot(p)
```

#### 19.4.2 Plain assignment

`T-Assign` requires: the target is a place (`IsPlace(p)`), its root variable is `var` (mutable), and the right-hand expression checks against the place type `T_p`:

```text
IsPlace(p)   PlaceRoot(p) = x   MutOf(Γ, x) = `var`   p :place T_p   e ⇐ T_p
```

Direct mutation of a `shared` place is valid when the Key System (Key chapter, §19.1.6) can form a valid `KeyPath(p)` with `RequiredMode(p) = Write` and no key scope, escape, or conflict rule forbids the access. If no covering write key is already held, the assignment implicitly acquires one through the ordinary access rules. `E-TYP-1604` applies only when no valid key-mediated write context can be formed.

#### 19.4.3 Compound assignment

`T-CompoundAssign` requires a `var` numeric place: the place type, with permission stripped, must be a primitive numeric type, and the right side must be a subtype of that numeric type:

```text
IsPlace(p)   PlaceRoot(p) = x   MutOf(Γ, x) = `var`
p :place T_p   StripPerm(T_p) = TypePrim(t)   t ∈ NumericTypes   e : T_e   T_e <: TypePrim(t)
```

`x op= e` reads the place once, evaluates `e`, applies the binary operator, and writes the result back (`ExecSigma-CompoundAssign`: `ReadPlaceSigma(p)` ⇒ evaluate `e` ⇒ `BinOp(op, v_p, v_e)` ⇒ `WritePlaceSigma(p, v)`).

#### 19.4.4 Diagnostics

- Target is not a place (`Assign-NotPlace`): `E-SEM-3131`.
- Assignment to an immutable `let` binding (`Assign-Immutable-Err`): `E-MOD-2401`.
- Type mismatch, or compound assignment on a non-numeric place (`Assign-Type-Err`): `E-SEM-3133`.
- Assignment / compound-assignment targets with effective `const` permission are rejected by Permission Admissibility (§10.4.7), reported as `E-TYP-1601` for both root places and subplaces.

#### 19.4.5 Worked example

```ultraviolet
procedure advanceCursor(state: var PlaybackState, delta: u32) -> () {
    state.frame_index += delta          // compound assignment on a numeric field
    state.is_dirty = true               // plain assignment to a bool field
}
```

### 19.5 Expression Statements (§18.5)

#### 19.5.1 Grammar

```ebnf
expr_stmt ::= expression terminator
```

An expression statement evaluates an expression for its effects and discards its value. It requires a terminator (newline or `;`).

`T-ExprStmt` requires only that the expression be well-typed (`e : T`); the resulting value is discarded and the statement leaves the environment unchanged. `ExecSigma-ExprStmt` evaluates the expression and maps its outcome to a statement outcome (`StmtOutOf`): a normal value becomes `ok`; a control transfer propagates.

```ultraviolet
flushQueue(render_queue)        // call evaluated for its effect; result discarded
counter.increment()             // method call as an expression statement
```

A block, `if`, `loop`, `unsafe`, or key block used in statement position is an expression statement when it is not the block tail. No additional diagnostics are introduced beyond those of the contained expression and the terminator rules of §19.1.

### 19.6 `defer` (§18.6)

#### 19.6.1 Grammar

```ebnf
defer_stmt ::= "defer" block_expr
```

`defer` schedules a block to run when the enclosing scope exits. The deferred block runs whether the scope exits normally, by `return`, by `break` / `continue`, or during a panic unwind.

#### 19.6.2 Semantics

- The deferred block must have type `()` (unit). A non-unit deferred block is `Defer-NonUnit-Err` (`E-SEM-3151`).
- The deferred block must **not** contain non-local control flow — no `return`, and no `break` / `continue` that would leave the deferred block. The predicate is `DeferSafe(b) ⇔ ¬ HasNonLocalCtrl(b, false)`. A `break` / `continue` *inside a loop that is itself inside the deferred block* is fine (the `in_loop` flag becomes `true`); what is forbidden is control that escapes the deferred block. A violation is `Defer-NonLocal-Err` (`E-SEM-3152`).
- Registration is dynamic: when execution reaches the `defer` statement, the block is appended to the scope's cleanup list (`ExecSigma-Defer` → `AppendCleanup`). A `defer` that is never reached is never registered and never run.

#### 19.6.3 Ordering — LIFO

Deferred blocks run in **reverse registration order** (last-in, first-out). Because cleanups are appended as they are reached and run from the most recent at scope exit, the deferred block registered last runs first. Deferred blocks and value destructors share the same cleanup list and unwind together at scope exit (`Cleanup-Step-Defer-*`).

#### 19.6.4 Worked example

```ultraviolet
procedure processBatch(source_path: string, dest_path: string) -> () {
    let input := openFile(source_path)
    defer {
        closeFile(input)
    }

    let output := createFile(dest_path)
    defer {
        closeFile(output)
    }

    copyContents(input, output)
}
```

Registration order is `input`'s defer, then `output`'s defer. At scope exit they run LIFO: `closeFile(output)` first, then `closeFile(input)`. If `copyContents` panics, both deferred blocks still run during unwind, in the same LIFO order.

### 19.7 `region` — Arena Scopes (§18.7)

#### 19.7.1 Grammar

```ebnf
region_stmt  ::= "region" region_opts? region_alias? block_expr
region_opts  ::= "(" expression ")"
region_alias ::= "as" identifier
```

A `region` statement opens an **arena** for the duration of its block. Allocations made into the arena with `new` or a `Region@Active` handle's `~>alloc` method (§19.7.4) live until the region's block exits, at which point the **entire arena is released at once** (`ReleaseArena`). A region thus amortizes many small allocations into one bulk reservation and one bulk free.

#### 19.7.2 Options and alias

- `region_opts` is a parenthesized expression of type `RegionOptions`. When omitted, the options default to a zero-argument `RegionOptions()` call:

  ```text
  RegionOptsExpr(⊥) = Call(Identifier(`RegionOptions`), [])
  ```

  `RegionOptions` is a public built-in record (`BuiltinRecord`) with two `public` fields, both defaulted:
  - `stack_size: usize` — bytes to pre-reserve for the arena (default `0`, meaning no pre-reservation; `RegionPrealloc(opts) = opts.stack_size`).
  - `name: string` — a diagnostic name (default the empty string).

- `region_alias` (`as name`) binds an identifier of type `unique Region@Active` to the region inside the block, so it can receive `~>alloc` calls or be handed to a `frame`. The region binding has type `TypePerm(unique, TypeModalState([Region], @Active))`.

  If no alias is given, the region binding is **synthetic**: it is not introduced by name resolution and cannot be referenced by user code (`T-RegionStmt`). Source code can still allocate into the anonymous region with `new`; use `as` only when code needs to name the region explicitly or pass it to a `frame`.

#### 19.7.3 Semantics

`T-RegionStmt`: the options expression is checked against `RegionOptions` (`opts ⇐ TypePath([RegionOptions])`), the region binding is introduced into a fresh scope by `RegionBind`, and the body is typed in that scope. The statement itself produces unit and leaves the outer environment unchanged.

Dynamically (`ExecSigma-Region`): evaluate the options, create a new arena (`RegionNew`), bind the alias if present (`BindRegionAlias`), evaluate the body in the region's scope (`EvalInScopeSigma`), then `RegionRelease` — run scope cleanup (deferred blocks / destructors), then `ReleaseArena` frees the whole arena, then pop the scope. A control transfer out of the body still releases the arena on the way out (`ExecSigma-Region` propagates `out'`).

#### 19.7.4 Allocating into a region

Current-region allocation uses `new`; explicit-target allocation uses the
Region modal method-call form:

```ebnf
new_expr        ::= "new" unary_expr
postfix_suffix ::= "~>" identifier "(" argument_list? ")"
```

`new value` allocates `value` into the innermost active scoped region.
`region_handle~>alloc(value)` allocates `value` into the region named by the
receiver. The receiver must type as `unique Region@Active`. The allocated
expression keeps its value type `T`; the result carries the target region's
provenance, so the value may not escape the region's lifetime. A value with
shorter-lived provenance escaping to a longer-lived location is `E-MEM-3020`.

#### 19.7.5 Worked example

```ultraviolet
procedure buildScene(scene_spec: SceneSpec) -> SceneReport {
    let report_count: usize = scene_spec.nodeCount()

    region (RegionOptions { stack_size: 64 * 1024, name: "scene" }) {
        let nodes := new allocateNodes(scene_spec)
        let edges := new allocateEdges(scene_spec, nodes)

        wireGraph(nodes, edges)
    }
    // Everything allocated into the scene region is released here, at once.

    return SceneReport { node_count: report_count }
}
```

`nodes` and `edges` live in the scene region; both are freed in one bulk release when the `region` block exits. Nothing allocated into the arena may escape the block (it would be a provenance escape, `E-MEM-3020`).

### 19.8 `frame` — Stack-Like Scopes (§18.8)

#### 19.8.1 Grammar

```ebnf
frame_stmt ::= "frame" block_expr | identifier "." "frame" block_expr
```

A `frame` is a **stack-like sub-scope of an existing region**. On entry it records a *mark* (`FrameMark`, the region's current high-water position); on exit it **resets the region back to that mark** (`ResetArena`), reclaiming everything the frame allocated while leaving earlier region allocations intact. Unlike a `region`, a `frame` does not own an arena — it borrows the enclosing region and rolls it back.

#### 19.8.2 Implicit vs explicit target

- **Implicit** — `frame { ... }`: targets the **innermost active region** in scope (`FrameBind(Γ, ⊥)` via `InnermostActiveRegion`). If there is no active region in scope, it is `Frame-NoActiveRegion-Err` (`E-MEM-1207`).
- **Explicit** — `region_alias.frame { ... }`: targets the named region. The identifier must resolve to a value whose type satisfies `RegionActiveType` (i.e. `Region@Active`); otherwise it is `Frame-Target-NotActive-Err` (`E-MEM-1208`).

`FrameBind` introduces a fresh synthetic region identifier `F` for provenance only; it carries the same synthetic-binding restriction as an anonymous `region` binding and cannot be referenced by user code. `new` inside a `frame` targets that frame scope. A named handle's `~>alloc` call still targets the named region explicitly.

#### 19.8.3 Semantics

Dynamically (`ExecSigma-Frame-Implicit` / `-Explicit`): resolve the target region, `FrameEnter` (push a scope, record `mark = FrameMark(...)`), evaluate the body in that scope, then `FrameReset` — run scope cleanup, then `ResetArena(..., mark)` rolls the region back to the recorded mark, then pop the scope. A control transfer out of the body still resets the region on the way out.

#### 19.8.4 Worked example

```ultraviolet
procedure renderFrames(scratch: unique Region@Active, work_items: [WorkItem]) -> () {
    loop item in work_items {
        // Each iteration allocates scratch data, then rolls it back.
        scratch.frame {
            let staging := new buildStaging(item)
            let temp_mesh := new tessellate(staging)
            submitToGpu(temp_mesh)
        }
        // `staging` and `temp_mesh` are reclaimed; `scratch` is reset to the mark.
    }
}
```

The region `scratch` is reused across every iteration. Each `frame` allocates into `scratch`, then resets it to the entry mark on exit, so peak memory is one iteration's worth rather than the whole loop's. The implicit `frame { ... }` form targets the innermost active region without naming it:

```ultraviolet
region as work_arena {
    frame {
        let buffer := new allocScratch(1024)
        useBuffer(buffer)
    }
    // work_arena reset to the mark taken at `frame` entry.
}
```

### 19.9 Control-Transfer Statements: `return`, `break`, `continue` (§18.9)

#### 19.9.1 Grammar

```ebnf
return_stmt   ::= "return" expression? terminator?
break_stmt    ::= "break" expression? terminator?
continue_stmt ::= "continue" terminator?
```

There are no loop labels. `break` and `continue` target the **innermost enclosing `loop`** (the Control-Flow chapter, §16.7, defines `loop`; the loop flag `L = loop` records that a loop encloses the statement). `return` exits the enclosing procedure / method / transition body.

#### 19.9.2 `return`

`return e` exits the current body, yielding `e`; `return` with no expression yields `()`.

- `T-Return-Value`: the return expression, viewed through `ReturnDestExpr` (which inserts `move` for a place or `copy` for a copy expression), is checked against the body's declared return type `R_b = BodyReturnType(R)`.
- `T-Return-Unit`: bare `return` is valid only when the body's return type is `()`.
- A returned owned place transfers its provenance/allocation domain to the return destination; a fresh expression materializes directly there; a `copy` first creates a duplicate domain that becomes the returned owner (`ReturnDestExpr`, `EvalReturnDest`, `LowerReturnDest`).

Diagnostics: return type mismatch (`Return-Type-Err`, also `Return-Unit-Err` for a non-unit body with bare `return`) is `E-SEM-3161`; invalid async return type is `Return-Async-Type-Err` / `Return-Async-Unit-Err`; `return` at module scope is `E-SEM-3165`.

A bare trailing `return e` as the final statement of a block makes the block's type `!` (never) via `BlockInfo-ReturnTail` (§19.1.3).

#### 19.9.3 `break`

`break` exits the innermost `loop`. It is valid only inside a loop (`L = loop`); outside a loop it is `Break-Outside-Loop` (`E-SEM-3162`).

- `T-Break-Value`: `break e` carries the value `e` out as a loop result type (`Brk = [T]`).
- `T-Break-Unit`: bare `break` carries unit and marks the loop as a void-break (`BrkVoid = true`).

A `loop` used as an expression takes its value from its `break` expressions. The loop type is computed by `LoopTypeInf` / `LoopTypeFin`: a loop with no `break` has type `()`; a loop whose value-breaks all agree on `T` (and which has no void-break) has type `T`; mixing value and void breaks, or value-breaks with no common type, is ill-typed.

#### 19.9.4 `continue`

`continue` skips to the next iteration of the innermost `loop`. It carries no value. It is valid only inside a loop (`L = loop`); outside a loop it is `Continue-Outside-Loop` (`E-SEM-3163`).

#### 19.9.5 Interaction with cleanup

Control transfer does not bypass cleanup. `return`, `break`, and `continue` each run pending `defer` blocks and value destructors for every scope they exit, in LIFO order, before the transfer completes. Temporary cleanup is emitted immediately before the transfer in lowering (`Lower-Stmt-Return` / `-Break` / `-Continue` with `TempCleanupIR`), and the returned owner is excluded from temporary cleanup (`TempCleanupIR(s \ ReturnedOwner(e))`) so the returned value is not dropped.

#### 19.9.6 Worked example

```ultraviolet
procedure firstReady(devices: [Device]) -> u32 {
    var index: u32 = 0
    loop device in devices {
        if device.isFaulted() {
            index += 1
            continue                 // next iteration of the innermost loop
        }
        if device.isReady() {
            return index             // exits the procedure; runs pending cleanup
        }
        index += 1
    }
    return index
}
```

A `loop` used as an expression takes its value from `break`. An infinite `loop` (no iterator, no condition) that exits only via `break candidate` has type `u32` because every `break` agrees on `u32`:

```ultraviolet
procedure findMatch(node_count: u32) -> u32 {
    var probe: u32 = 0
    let found_index: u32 = loop {
        if probe == node_count {
            break node_count         // sentinel: completed without a match
        }
        if isMatch(probe) {
            break probe              // loop evaluates to this value
        }
        probe += 1
    }
    return found_index
}
```

Both `break` expressions carry `u32`, so the loop's value type is `u32`. (Ultraviolet has no `loop ... else`: a loop's fall-through value is `()`, so a loop used for a non-unit value must reach every exit through a value-carrying `break`.)

### 19.10 `unsafe` Statements (§18.10)

#### 19.10.1 Grammar

```ebnf
unsafe_block ::= "unsafe" block_expr
```

An `unsafe` statement runs its block in an unsafe context, permitting operations that require an explicit unsafe boundary. The block is a normal block: it has whatever type its tail produces (`T-UnsafeStmt`: `b : T`), and its value/outcome are exactly the block's (`ExecSigma-UnsafeStmt`). `unsafe` is also available in expression position (`unsafe_expr`, §16.8.1); the statement form is the same construct used where a statement is expected.

#### 19.10.2 What `unsafe` enables

The `unsafe` block does **not** introduce diagnostics of its own. Instead, specific operations require being inside an unsafe span (`UnsafeSpan(...)`), and each owning construct reports the violation when it is used outside one. Operations that require `unsafe` include:

- references to packed fields under `#layout(packed)` (`AddrOfOk` requires `UnsafeSpan` when `PackedField(p)`),
- `transmute` (`Transmute-Unsafe-Err`),
- raw allocator operations — raw `alloc` / `dealloc` (`AllocRaw-Unsafe-Err`, `DeallocRaw-Unsafe-Err`),
- unchecked region operations such as `Region::reset_unchecked` and `Region::free_unchecked` (`Region-Unchecked-Unsafe-Err`),
- raw-pointer dereference (`T-Deref-Raw`),
- `extern` (foreign) calls.

The unsafe-required-operation diagnostic is `E-MEM-3030` (owning rules: `AllocRaw-Unsafe-Err`, `DeallocRaw-Unsafe-Err`, `Region-Unchecked-Unsafe-Err`, `Transmute-Unsafe-Err`).

#### 19.10.3 Worked example

```ultraviolet
procedure reinterpretBits(raw: u32) -> f32 {
    // `transmute<T1, T2>(e)` requires an unsafe context.
    let bits: f32 = unsafe {
        transmute<u32, f32>(raw)
    }
    return bits
}
```

Keep the `unsafe` block as small as possible — exactly the operation that needs it — and wrap it in a safe API that re-establishes project invariants (style guide; see §19.11).

### 19.11 Idioms & Best Practices

Grounded in the Ultraviolet style guide (AGENTS.md) and the spec semantics above:

- **Prefer `let` to `var`.** Reach for `var` only when the binding is genuinely reassigned. Immutable bindings communicate intent and let the checker prove more. Use `:=` for owning, must-not-escape resources whose destructor must run in place.
- **Let the block tail carry the value.** Express a block's result as a bare tail expression rather than assigning to an outer `var` and falling out. This keeps block types precise (§19.1.3).
- **Newlines terminate statements.** Use `;` only to justify several short statements on one line; do not pepper code with semicolons. Use same-line K&R braces and 4-space indentation, targeting a 100-column maximum.
- **Write explicit `return` in non-`unit` procedures.** The style guide prefers explicit `return` for clarity even where a tail expression would also work.
- **Pair acquisition with `defer` immediately.** Register cleanup right after the resource is acquired, so the LIFO order mirrors acquisition order and no path can skip it. Prefer `defer` over hand-written cleanup before each `return`.
- **Use `region` for bulk-lifetime allocation, `frame` for per-iteration scratch.** A `region` amortizes many allocations into one bulk free; a `frame` resets a region to a mark so a loop reuses scratch memory without per-iteration heap traffic. Use `new` for the current scope. Name a region with `as` when code must target it explicitly or hand it to a `frame`; leave it anonymous when current-scope allocation is enough.
- **Keep `unsafe` minimal and wrapped.** Make the `unsafe` block exactly the operation that requires it, and expose a safe wrapper that restores invariants. More code is not a justification for widening an `unsafe` span.
- **Alias with `using ... as` only when it earns its place.** Do not use a local `using` to simulate shadowing or to rename for cosmetics. Wildcard `using module::*` is reserved for internal or implementation modules; never use it in a public API surface.
- **Do not let allocated values escape their region/frame.** Treat the region/frame block as the lifetime boundary; return owned heap values, not arena-provenance values.

### 19.12 Pitfalls & Diagnostics

| Situation | Rule | Code |
| --- | --- | --- |
| Assigning to a `let` binding | `Assign-Immutable-Err` | `E-MOD-2401` |
| Assignment target is not a place expression | `Assign-NotPlace` | `E-SEM-3131` |
| Assignment type mismatch / annotation mismatch | `Assign-Type-Err`, `T-LetStmt-Ann-Mismatch` | `E-SEM-3133` |
| Compound assignment on a non-numeric place | `Assign-Type-Err` | `E-SEM-3133` |
| Compound assignment on a `let` place | `Assign-Immutable-Err` | `E-MOD-2401` |
| Assigning through a `const`-permission place | Permission Admissibility (§10.4.7) | `E-TYP-1601` |
| `shared` write with no valid key-mediated context | §10.4.7 / Key System | `E-TYP-1604` |
| `defer` block has non-unit type | `Defer-NonUnit-Err` | `E-SEM-3151` |
| `return` / `break` / `continue` escaping a `defer` block | `Defer-NonLocal-Err` | `E-SEM-3152` |
| `return` type mismatch with the body | `Return-Type-Err`, `Return-Unit-Err` | `E-SEM-3161` |
| `break` outside a `loop` | `Break-Outside-Loop` | `E-SEM-3162` |
| `continue` outside a `loop` | `Continue-Outside-Loop` | `E-SEM-3163` |
| `return` at module scope | — | `E-SEM-3165` |
| `frame` with no active region in scope | `Frame-NoActiveRegion-Err` | `E-MEM-1207` |
| `r.frame` target not in `Region@Active` | `Frame-Target-NotActive-Err` | `E-MEM-1208` |
| `new` allocation with no active region in scope | `New-NoActiveRegion-Err` | `E-MEM-3021` |
| Arena value escaping its region/frame (shorter-lived provenance) | provenance escape | `E-MEM-3020` |
| `unique` binding from a place without `move` | `B-LetVar-UniqueNonMove-Err` | `E-MEM-3007` |
| Moving from an immovable (`:=`) binding | `Trans-Let-NoReassign`, `B-Closure-MoveCapture-Immovable-Err` | `E-MEM-3006` |
| Unsafe-required op outside an `unsafe` block | `AllocRaw-Unsafe-Err`, `DeallocRaw-Unsafe-Err`, `Region-Unchecked-Unsafe-Err`, `Transmute-Unsafe-Err` | `E-MEM-3030` |
| Refutable pattern in `let` / `var` | `Let-Refutable-Pattern-Err` | — |

Common traps:

- **A trailing terminator turns the tail into a statement.** `{ ...; value }` has type `T`, but `{ ...; value; }` has type `()` — the `;` makes `value` an expression statement and the block falls out with unit. If a block "lost its value," check for a stray terminator after the last expression (§19.1.3).
- **`break value` and the loop result type.** Every value-`break` in one loop must agree on a common type, and a loop must not mix a value-`break` with a bare void-`break`. A loop with no `break` has type `()` (`LoopTypeFin`); to use a loop for a non-unit value, reach every exit through a value-carrying `break`.
- **`defer` that is never reached never runs.** Registration is dynamic. If a `defer` sits after an early `return`, it is not registered on that path. Register cleanup as early as possible, right after acquisition.
- **Refutable pattern in `let`.** `let Outcome::Value(x) = result` is rejected (`Let-Refutable-Pattern-Err`); use `if result is Outcome::Value(x) { ... }` or a `loop ... in` instead.
- **`frame` needs a region.** A bare `frame { ... }` with no enclosing `region` (and no in-scope `Region@Active`) is `E-MEM-1207`. Open a `region` first, or pass an active region and use `region_alias.frame`.
- **No loop labels, no `loop ... else`.** `break` / `continue` always bind to the innermost `loop`, and `else` attaches only to `if` forms (`ElseCont`/§16.7.1), never to a loop. To affect an outer loop, restructure with a flag, a `var`, or an early `return`.
- **Control-flow bodies parse through `ParseBlock`.** The grammar reads every `if` / `loop` / `else` body via `ParseBlock`, which requires `{` (§18.1.2). Keep braces on control-flow bodies so the code parses as written.
