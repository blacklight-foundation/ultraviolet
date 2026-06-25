## 21. Authority, Capabilities, Regions & the Memory Model

Ultraviolet enforces a single, uncompromising rule about side effects: **nothing observable happens unless you hold a capability value and call through it.** There is no ambient authority — no global `print`, no implicit allocator, no hidden clock, no environment access that "just works." Every externally observable effect is gated behind a capability handle that must be threaded explicitly from the program entry point. This chapter specifies the authority model (§6.1), the runtime host primitives that capabilities mediate (§6.2), binding and permission runtime state (§6.3), the arena memory model with regions, frames, and provenance (§6.4), the dynamic scope stack and region runtime (§6.5), and the runtime/memory diagnostics that backstop all of it (§6.6).

This chapter is the *why* behind explicit effects. For the type-level surface of
capability classes and `$Class` dynamic objects, see "Refinement & Capability
Classes." For modal types like `File@Read` and `Region@Active`, see "Modal
Types & Transitions." For the `Outcome<T, E>` error type and the `?`
propagation operator, see "Expressions & Operators." For the `region`/`frame`
statement surface, see "Statements & Regions."

One token convention used throughout: the result type that the **programmer sees** for a capability method is the type declared in the capability *interface* (§14.9) — for example, `io~>open_read(path)` has type `Outcome<File@Read, IoError>`. This matches the lower-level host-primitive relations of §6.2 (`IOResType(IOOpenRead) = Outcome<File@Read, IoError>`). Always type your bindings against the interface return type.

### 21.1 The Authority Model (§6.1)

#### 21.1.1 No Ambient Authority

Ultraviolet adopts a **no ambient authority** discipline (§6.1): observable external effects are possible only through explicit possession and use of capability values. A *capability value* is a value whose type names a capability class.

The built-in (root) capability classes are fixed (§6.1.1):

```text
BuiltinCapabilityClass = {IO, Network, HeapAllocator, Reactor, ExecutionDomain,
                          System, Time, MonotonicTime, WallTime}
```

Capability classes are ordinary classes. A user class becomes a capability class iff it declares a capability superclass via `<:` (`CapClass(p)`); the built-in classes above are the *root* capability classes. Capability classhood is determined by the superclass relation alone and MUST NOT depend on attributes or naming (§14.9.4).

The function `CapInType` extracts the capability classes carried by a type. The most important facts for everyday code:

```text
CapInType(TypePath(["Context"])) = {IO, Network, HeapAllocator, Reactor,
                                    ExecutionDomain, System, Time}
CapInType(TypeDynamic([p]))      = {p}    if CapClass(p)
CapInType(TypeDynamic([p]))      = ∅      if ¬ CapClass(p)
CapInType(TypePerm(_, T))        = CapInType(T)
```

So `$IO`, `$Network`, `$HeapAllocator`, and `Context` are all capability-bearing types. `CapInType` distributes structurally over tuples, arrays, slices, permission-qualified types (`TypePerm`), and compound nominal/modal/union/applied types after alias expansion.

Two closures lift capabilities upward and forward:

- `CapUp(c)` closes a capability over its capability ancestors, so a derived capability satisfies a requirement stated against an ancestor.
- `CapDerive(c)` grants what a class's own interface can *mint*. The canonical built-in case is `DeriveSet(Time) = {MonotonicTime, WallTime}`: a `$Time` capability can produce `$MonotonicTime` and `$WallTime`.

`EffectiveCaps(T) = ⋃{ CapUp(c) ∪ CapDerive(c) | c ∈ CapInType(T) }`.

#### 21.1.2 The Four No-Ambient-Authority Requirements

A conforming implementation MUST satisfy (§6.1.2):

- **(NAA-1) No implicit capability roots.** A conforming implementation MUST NOT provide any implicit or global binding whose type is capability-bearing under `CapInType`. You cannot reach `IO` out of thin air.
- **(NAA-2) Context as the sole explicit root carrier.** The only capability roots introduced at runtime are those contained in `Context` values produced by `ContextInitSigma` (executable entry, §24.4.5) or `HostSessionInitSigma` (hosted-library session, §24.4.4). Roots enter the program *only* through the entry procedure or a sanctioned hosted-library session.
- **(NAA-3) Effect gating.** Any externally observable effect MUST occur only as a consequence of calling a runtime host primitive (§6.2) or a built-in procedure/method whose receiver is a capability value.
- **(NAA-4) User capabilities confer no new root authority.** Constructing a value of a user-defined capability class requires no ambient grant; it confers authority only through the built-in capability values it encapsulates, and every effect remains gated by NAA-3.

The static enforcement of effect gating is the **capability-flow rule**. For every declaration `d`:

```text
CapReq(d)          = ⋃{CapInType(T_i)    | T_i is the type of a parameter or receiver of d}
EffectiveCapReq(d) = ⋃{EffectiveCaps(T_i) | T_i is the type of a parameter or receiver of d}
```

> For every direct call from `d_src` to `d_tgt`, a conforming implementation MUST reject the program unless `CapReq(d_tgt) ⊆ EffectiveCapReq(d_src)`.

In plain terms: **a procedure may call only what its own parameters' capabilities authorize.** A procedure that takes no capability cannot call a procedure that requires one. This is the mechanism that makes "this function cannot touch the filesystem" a checkable, compile-time fact rather than a convention.

#### 21.1.3 The Idiomatic Entry Point and the Capability Call Operator `~>`

An executable's entry procedure is `main`. Its signature is fixed by `MainSigOk` (§24):

```text
MainSigOk(d) ⇔ d = ProcedureDecl(_, vis, `main`, _, _, params, ret_opt, _, _, _, _)
             ∧ vis = `public` ∧ params = [⟨mode, name, ty⟩] ∧ mode ∈ {⊥, `move`}
             ∧ ContextBundleType(StripPerm(ty)) ∧ ret_opt = TypePrim("i32")
```

That is: `main` is `public`, takes exactly one parameter whose type is `Context` (or a context-bundle record built only from context capability fields), and returns `i32`. The runtime constructs the single `Context` argument via `ContextInitSigma`/`ContextBundleBuild` and passes it in. This is the *only* place capability roots originate.

The `Context` record's fields are the capability roots (`ContextFields`, §14.9):

| Field | Type | Capability |
| --- | --- | --- |
| `io` | `$IO` | filesystem and standard streams |
| `net` | `$Network` | network |
| `heap` | `$HeapAllocator` | heap allocation |
| `sys` | `$System` | environment, args, process control |
| `reactor` | `$Reactor` | async reactor |
| `time` | `$Time` | clocks |

`Context` also has methods `cpu()`, `gpu()`, and `inline()`, each returning `$ExecutionDomain` (see "Concurrency & Parallelism"). Note the field order in `ContextFields` is `io, net, heap, sys, reactor, time`.

Calls on capability (dynamic `$Class`) values use the **capability call operator `~>`**. Its grammar (Appendix B.3 and §16):

```ebnf
postfix_suffix   ::= "." identifier | "." decimal_integer | "[" expression "]"
                   | "~>" identifier "(" argument_list? ")" | "(" argument_list? ")" | "?"

method_call_expr ::= postfix_expr "~>" identifier "(" argument_list? ")"
```

`~>` is the method/transition-call operator for dynamic class objects and modal values (§14.6, §13). On a `$IO` value, `io~>write_stdout(data)` performs a call dispatched through the capability's vtable. Field access uses `.`; the capability *call* always uses `~>`. The typing rule **(T-Dynamic-MethodCall)** (§14.6.4) requires the named method to exist on the class (`LookupClassMethod(Cl, name) = m`) and the receiver permission to admit the method's receiver permission (`PermAdmits(P_caller, P_method)`); the result type is `ReturnType(m)`.

The canonical `main` obtains `ctx.io` and invokes a method through `io~>...`. Because `write_stdout` returns `Outcome<(), IoError>` (a two-variant enum with variants `Value` and `Error`), you narrow it with an enum pattern in an `if ... is` block:

```ultraviolet
public procedure main(ctx: Context) -> i32 {
    let io: $IO = ctx.io
    let result: Outcome<(), IoError> = io~>write_stdout("Hello, Ultraviolet\n")

    if result is Outcome::Error(e) {
        return 1
    }

    return 0
}
```

The `Context` parameter is the sole reason this program may write to stdout at all. A procedure that did not receive `$IO` (directly or via `Context`) could not call `write_stdout` without being rejected by the capability-flow rule (NAA-3). This is *why* effects are explicit: authority is data, threaded by hand, and the compiler proves every effect traces back to a root that entered through `main`.

To keep authority narrow (style guide §"Capability Passing": "Pass only the exact capabilities a procedure or method uses"), helpers should take the *specific* capability they need, not the whole `Context`. Inside a fallible procedure you propagate `Outcome` results with `?`:

```ultraviolet
procedure greet(io: $IO, name: string@View) -> Outcome<(), IoError> {
    io~>write_stdout("Hello, ")?
    io~>write_stdout(name)?
    io~>write_stdout("\n")?
    return Outcome::Value(())
}

public procedure main(ctx: Context) -> i32 {
    if greet(ctx.io, "world") is Outcome::Error(e) {
        return 1
    }
    return 0
}
```

`greet` requires `{IO}` and nothing else; it provably cannot touch the network, the clock, or the heap. `Outcome` is a two-variant enum, so the success value of an `Outcome<(), IoError>` is constructed as `Outcome::Value(())` (or introduced implicitly by `return`ing the payload in an `Outcome`-typed context). In *pattern* position you write the enum-variant pattern `Outcome::Value(x)` / `Outcome::Error(e)`.

#### 21.1.4 Attenuation

Capabilities can be *narrowed* but never *widened*. The attenuation operations (§6.1.3) each derive a strictly-weaker child capability from a parent:

```text
$IO::restrict(root)
$Network::restrict_to_host(host)
$HeapAllocator::with_quota(bytes)
CancelToken@Active::child()
Context::cpu()  /  Context::gpu()  /  Context::inline()
$Time::monotonic()  /  $Time::wall()
$MonotonicTime::coarsen(resolution)  /  $WallTime::coarsen(resolution)
```

Attenuation MUST be **monotone**: a derived capability MUST NOT grant authority beyond its source. For every `ChildCap = ParentCap~>attenuate(...)`, a conforming implementation MUST enforce:

- `ChildCap` is operational only while `ParentCap` is live.
- Dropping `ChildCap` does not invalidate or diminish `ParentCap`.
- Dropping `ParentCap` while a derived child is still live is ill-formed.
- Any runtime delegation by `ChildCap` is routed through `ParentCap` (or an equivalent object enforcing an equal-or-stricter subset).

The idiomatic use is to hand subsystems a *restricted* capability. For example, `io~>restrict(path)` returns a `$IO` whose every operation is confined to a base directory (the `RestrictPath` relation of §6.2.1):

```ultraviolet
public procedure main(ctx: Context) -> i32 {
    let sandbox_io: $IO = ctx.io~>restrict("assets")

    // sandbox_io can only see paths under "assets".
    let present: bool = sandbox_io~>exists("textures/sky.png")

    if present {
        return 0
    }
    return 1
}
```

`restrict` is monotone: `sandbox_io` can reach a subset of what `ctx.io` could, never more. A heap example with `with_quota` narrows allocation authority to a byte budget:

```ultraviolet
procedure runBounded(heap: $HeapAllocator) -> () {
    let limited: $HeapAllocator = heap~>with_quota(65536usize)
    // `limited` rejects allocations once 65536 bytes are outstanding.
}
```

#### 21.1.5 Observable Behavior, the As-If Rule, and Sequence Points

The set of observable effects (§6.1.4) is fixed:

```text
ObservableEffect ∈ { HostEffect(proc, args), FfiEffect(proc, abi, dir),
                     PanicEffect(kind), DropEffect(target), KeyEffect(kind, paths) }
```

An event is observable iff it is a runtime host-primitive action (from `HostPrimRuntime`), a transfer across an FFI boundary, a panic initiation / panic-to-abort escalation / caught-unwind boundary result, an invocation of drop cleanup for a responsible value or static, or a key acquisition / release / ordered-commit. The **observable behavior** of an execution is the ordered trace of its observable events plus its final outcome in `{normal, panic, abort}`.

The **as-if rule**: an implementation MAY apply any transformation whose executions preserve the observable-event trace at every sequence point, the required relative order of drop actions (per `CleanupScope`, `Unwind`, `Destroy`), the relative order of capability-mediated host effects and FFI effects, the panic/non-panic/abort outcomes (including `#unwind` boundary behavior), the key-system behavior, and the permission/provenance/responsibility facts of Chapters 6, 10, 18, 23, and 24. It MUST NOT eliminate, duplicate, or reorder observable events; suppress or invent a required `Drop`; reorder host/FFI effects across sequence points; turn a panicking execution non-panicking (or the reverse); or violate the key, provenance, or no-ambient-authority rules.

A **sequence point** (§6.1.5) is a program point at which all prior observable effects are complete, no later effect has begun, and binding/provenance/key-holding state is determined. The canonical sequence points are: after each terminated statement; after receiver and argument evaluation and before control enters a callee; after the left operand of `&&`/`||`; after the condition of `if`; after the scrutinee of `if ... is`; after key-path evaluation and before a key block acquires or commits keys; and immediately before scope cleanup begins on ordinary scope exit, `return`, `break`, `continue`, panic unwinding, and FFI-boundary unwinding.

`unsafe` operations and the FFI MAY escape the no-ambient-authority constraints by design, but capability isolation still applies (§6.1.6; see "Unsafe & Foreign Function Interface").

### 21.2 Host Primitives (§6.2)

Capability methods are surface syntax over **host primitives** — the relations through which the abstract machine actually performs effects. They are partitioned by capability:

```text
IOPrim     = {IOOpenRead, IOOpenWrite, IOOpenAppend, IOCreateWrite, IOReadFile,
              IOReadBytes, IOWriteFile, IOWriteStdout, IOWriteStderr, IOExists,
              IORemove, IOOpenDir, IOCreateDir, IOEnsureDir, IOKind, IORestrict}
FilePrim   = {FileReadAll, FileReadAllBytes, FileWrite, FileFlush, FileClose}
DirPrim    = {DirNext, DirClose}
SystemPrim = {SystemGetEnv, SystemExecutablePath, SystemArgumentCount,
              SystemArgument, SystemCurrentDirectory, SystemExit, SystemRun}
NetworkPrim = {NetRestrictHost}
HeapPrim    = {HeapWithQuota, HeapAllocRaw, HeapDeallocRaw}
ReactorPrim = {ReactorRun, ReactorRegister}
TimePrim    = {TimeMonotonic, TimeWall, MonotonicTimeNow, MonotonicTimeResolution,
               MonotonicTimeElapsed, MonotonicTimeCoarsen, WallTimeNowUtc,
               WallTimeResolution, WallTimeCoarsen}
CancelPrim  = {CancelNew, CancelChild, CancelDoCancel, CancelIsCancelled,
               CancelWaitCancelled}
```

`HostPrimRuntime` is the union of all of the above (the primitives that produce observable runtime effects). A separate `HostPrimDiag` set (`ParseTOML`, `ReadBytes`, `WriteFile`, `ResolveTool`, `ResolveRuntimeLib`, `Invoke`, `AssembleIR`, `InvokeLinker`, `InvokeArchiver`, `ArchiveMembers`) covers build/diagnostic host primitives. A host primitive that can fail but maps to neither a diagnostic nor a runtime classification is ill-formed (`HostPrimFail(p) ∧ ¬ MapsToDiagOrRuntime(p) ⇒ IllFormed(p)`).

These primitives are reached through the capability *interfaces* by the `PrimCall` rules (§6.2.5). For example `Γ ⊢ PrimCall(IO, write_stdout, v_io, [d]) ⇓ Val(r)` where `IOWriteStdout(v_io, d) ⇓ r`. You never name a primitive in source; you call the interface method.

#### 21.2.1 IO, File, and Directory Primitives

The `$IO` interface (`IOInterface`, §14.9) is the surface you write. Every fallible operation returns an `Outcome<T, IoError>`:

| Method | Surface result type |
| --- | --- |
| `open_read(path)` | `Outcome<File@Read, IoError>` |
| `open_write(path)` | `Outcome<File@Write, IoError>` |
| `open_append(path)` | `Outcome<File@Append, IoError>` |
| `create_write(path)` | `Outcome<File@Write, IoError>` |
| `read_file(path)` | `Outcome<unique string@Managed, IoError>` |
| `read_bytes(path)` | `Outcome<unique bytes@Managed, IoError>` |
| `write_file(path, data)` | `Outcome<(), IoError>` |
| `write_stdout(data)` | `Outcome<(), IoError>` |
| `write_stderr(data)` | `Outcome<(), IoError>` |
| `exists(path)` | `bool` |
| `remove(path)` | `Outcome<(), IoError>` |
| `open_dir(path)` | `Outcome<DirIter@Open, IoError>` |
| `create_dir(path)` | `Outcome<(), IoError>` |
| `ensure_dir(path)` | `Outcome<(), IoError>` |
| `kind(path)` | `Outcome<FileKind, IoError>` |
| `restrict(path)` | `$IO` |

Every IO method's receiver is `const` and every `path`/`data` parameter is `string@View` — except `write_file`'s `data`, which is `bytes@View`. (`write_stdout` and `write_stderr` take `data: string@View`.)

`IoError` is an enum with variants `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidPath`, `Busy`, `IoFailure`, `DirectoryNotEmpty`. `FileKind` is an enum with variants `File`, `Dir`, `Other`.

The IO runtime carries an abstract state `ω = ⟨entries, handles, diriters, flushed, failmap⟩`. The error semantics are fully specified (§6.2.1); for example: a path-form-zero op on an invalid path yields `IoError::InvalidPath`; an op requiring an existing entry on a missing path yields `IoError::NotFound`; `create_write` on an existing path yields `IoError::AlreadyExists`; `remove` on a non-empty directory yields `IoError::DirectoryNotEmpty`; `read_file`/`read_all` over non-UTF-8 bytes yields `IoError::IoFailure`.

`File` is a **modal** type with states `@Read`, `@Write`, `@Append`, `@Closed`. On a `File@Read` value the **state methods** `read_all() -> Outcome<unique string@Managed, IoError>` and `read_all_bytes() -> Outcome<unique bytes@Managed, IoError>` are called with `~>`; on `File@Write`/`File@Append` the state methods are `write(data: bytes@View) -> Outcome<(), IoError>` and `flush() -> Outcome<(), IoError>`. `close` is a **transition** (`transition close() -> @Closed`), also invoked with `~>`, producing a `File@Closed`.

`DirIter` is modal with states `@Open`, `@Closed`. On `DirIter@Open`, `next()` returns `Outcome<DirEntry | (), IoError>` — the success payload is itself the union `DirEntry | ()`, where `()` signals exhaustion. `close` is a transition to `@Closed`. `DirEntry` is a record with fields `name: string@Managed`, `path: string@Managed`, `kind: FileKind`; directory listings are sorted by case-folded NFC entry key (`EntryOrder`).

Reading a file and printing it, propagating `Outcome` with `?` and narrowing at the boundary:

```ultraviolet
procedure printFile(io: $IO, path: string@View) -> Outcome<(), IoError> {
    let contents: unique string@Managed = io~>read_file(path)?
    io~>write_stdout(contents)?
    return Outcome::Value(())
}

public procedure main(ctx: Context) -> i32 {
    if printFile(ctx.io, "notes.txt") is Outcome::Error(e) {
        return 1
    }
    return 0
}
```

Opening a directory and iterating it: `open_dir` returns an `Outcome`, matched with `Outcome::Value` / `Outcome::Error`. The `next` method returns `Outcome<DirEntry | (), IoError>`, matched the same way — its success payload is the `DirEntry | ()` union, which is then narrowed by type:

```ultraviolet
procedure listDir(io: $IO, path: string@View) -> Outcome<(), IoError> {
    let opened: Outcome<DirIter@Open, IoError> = io~>open_dir(path)

    if opened is Outcome::Error(e) {
        return Outcome::Error(e)
    }

    if opened is Outcome::Value(iter) {
        loop {
            let step: Outcome<DirEntry | (), IoError> = iter~>next()
            if step is Outcome::Error(e) {
                return Outcome::Error(e)
            }
            if step is Outcome::Value(item) {
                if item is entry: DirEntry {
                    io~>write_stdout(entry.name)?
                    io~>write_stdout("\n")?
                } else {
                    break
                }
            }
        }
        let closed: DirIter@Closed = iter~>close()
    }

    return Outcome::Value(())
}
```

`IORestrict` confines an `$IO`: for a non-absolute `path` joined under the canonicalized base, the restricted IO routes the op to the joined path; an out-of-base or absolute path yields `IoError::InvalidPath` (or `false` for `exists`), per `RestrictPath`.

#### 21.2.2 System Primitives

`$System` (the `SystemInterface`) provides `exit(code: i32) -> !`, `get_env(key: string@View) -> string@View`, `executable_path() -> string@View`, `argument_count() -> usize`, `argument(index: usize) -> string@View`, `current_directory() -> string@View`, and `run(command: string@View) -> i32`. The runtime state is `SysState = ⟨env, exit_code_opt⟩`. `SystemGetEnv` returns the value if `key ∈ dom(Env)`, otherwise an empty `string@View`. `SystemExit(code)` sets the exit code and yields `Ctrl(Abort)` — `PrimCall(System, exit, ...)` terminates the program with observable exit status `code` (this is why `exit` is typed `!`). `SystemRun(command)` runs a host command and returns its `i32` code.

```ultraviolet
public procedure main(ctx: Context) -> i32 {
    let sys: $System = ctx.sys
    let count: usize = sys~>argument_count()

    if count < 2usize {
        let _ = ctx.io~>write_stderr("usage: tool <name>\n")
        return 2
    }

    let name: string@View = sys~>argument(1usize)
    let _ = ctx.io~>write_stdout(name)
    return 0
}
```

`let _ = ...` discards an `Outcome<(), IoError>` result deliberately; in a procedure returning `Outcome` you would instead propagate with `?`.

#### 21.2.3 Time Primitives

`$Time` is the process time root and mints two derived clocks: `time~>monotonic()` returns `$MonotonicTime`, `time~>wall()` returns `$WallTime` (these are `CapDerive(Time)`). Both are attenuation relations: the derived clock's authority is a subset of `$Time`.

`$MonotonicTime` (the `MonotonicTimeInterface`) provides `now() -> MonotonicInstant`, `resolution() -> Duration`, `elapsed(start: MonotonicInstant, end: MonotonicInstant) -> Outcome<Duration, TimeError>`, and `coarsen(resolution: Duration) -> Outcome<$MonotonicTime, TimeError>`. Monotonicity is guaranteed within a clock domain: for two successful reads through capabilities in the same domain, if read A happens-before read B then `ticks_A <= ticks_B`. `elapsed` requires both instants to come from the authorized clock domain with `end` not preceding `start`, else `ClockMismatch` or `OutOfRange`.

`$WallTime` (the `WallTimeInterface`) provides `now_utc() -> Outcome<UtcInstant, TimeError>` (UTC nanoseconds since the Unix epoch), `resolution() -> Outcome<Duration, TimeError>`, and `coarsen(resolution: Duration) -> Outcome<$WallTime, TimeError>`. `TimeError` variants: `Unsupported`, `ClockUnavailable`, `OutOfRange`, `InvalidResolution`, `ClockMismatch`. `coarsen` requires a positive duration and yields `InvalidResolution` otherwise; the coarsened clock MUST NOT expose precision finer than `max(n, resolution(source))` and MUST NOT invalidate the source. `Duration` is a record with field `nanoseconds: u128`; `MonotonicInstant` has private fields `domain: usize`, `ticks: u128`; `UtcInstant` has field `unix_nanoseconds: i128`.

Use the *monotonic* clock for elapsed-time measurement and the *wall* clock for calendar time — never the reverse:

```ultraviolet
procedure timeRun(time: $Time) -> Outcome<Duration, TimeError> {
    let clock: $MonotonicTime = time~>monotonic()
    let start: MonotonicInstant = clock~>now()

    doWork()

    let finish: MonotonicInstant = clock~>now()
    return clock~>elapsed(start, finish)
}
```

#### 21.2.4 Network Primitives

`$Network` (the `NetworkInterface`) provides `restrict_to_host(host: string@View) -> $Network` (`NetRestrictHost`). The derived `$Network` has authority that is a subset of the source; any connection/bind/name-resolution through it MUST be rejected (before any observable network effect) unless its effective host equals `host`. `restrict_to_host` MUST NOT invalidate the source or mutate unrelated capability state.

#### 21.2.5 Heap and Reactor Primitives

`$HeapAllocator` (the `HeapAllocatorInterface`) provides `with_quota(size: usize) -> $HeapAllocator` (attenuation by byte budget), plus the raw primitives `alloc_raw(count: usize) -> *mut u8` and `dealloc_raw(ptr: *mut u8, count: usize) -> ()`. **Calls to `alloc_raw` and `dealloc_raw` require an `unsafe` context** (`AllocRaw-Unsafe-Err`, `DeallocRaw-Unsafe-Err`); `with_quota` does not. `AllocationError` has variants `OutOfMemory(usize)` and `QuotaExceeded(usize)`.

`$Reactor` provides the generic methods `run<T; E>(future: Future<T, E>) -> T | E` and `register<T; E>(future: Future<T, E>) -> Tracked<T, E>` (see "Async & the Reactor"). Note the Ultraviolet generic-parameter separator is `;`, not `,`.

```ultraviolet
procedure allocBytes(heap: $HeapAllocator) -> *mut u8 {
    let limited: $HeapAllocator = heap~>with_quota(4096usize)
    unsafe {
        let block: *mut u8 = limited~>alloc_raw(256usize)
        return block
    }
}
```

### 21.3 Binding and Permission Runtime State (§6.3)

The static checker tracks, per binding, whether it is fully valid, fully moved, or partially moved, and whether its `unique` permission is currently active. This is the machinery that backs move/borrow checking; it is consumed by the feature chapters (assignments, calls, closures, patterns).

#### 21.3.1 Binding State

```text
BindingState ::= Valid | Moved | PartiallyMoved(F)    (F ⊆ Name)

Movability     ::= mov | immov
Responsibility ::= resp | alias
Mutability     = {`let`, `var`}
BindInfo       ::= ⟨state, mov, mut, resp⟩

BindScope = Map(Identifier, BindInfo)
𝔅         = [BindScope]
```

A binding stack `𝔅` is a list of scopes. `Lookup_B` searches innermost-first; `Update_B` updates the nearest scope that defines the name; `Intro_B` introduces a name in the top scope. The `resp` (responsibility) component distinguishes a *responsible* binding (`resp` — owns the value, drives drop) from an *alias* (`alias` — borrows, no drop). The `mov`/`immov` component records whether the binding may be moved from (introduced with `=`, `MovOf("=") = mov`) or not (introduced with `:=`, `MovOf(":=") = immov`).

#### 21.3.2 Permission Activity State

`unique` permissions can be temporarily *suspended* while a unique borrow is outstanding. Permission state is keyed by binding identifier plus field path:

```text
PermOf(TypePerm(p, T)) = p
PermOf(T)              = `const`    if T ≠ TypePerm(_, _)

ActiveState ::= Active | Inactive
PermKey     = Identifier × FieldPath
PermScope   = Map(PermKey, ActiveState)
Π           = [PermScope]
```

`Lookup_Π` returns `Inactive` if any scope marks the key inactive, else `Active` (the default for absent keys). A `unique` place is accessible only when every ancestor path key is `Active` (`AccessPathOk`).

#### 21.3.3 State Transitions

Moving a whole binding makes it `Moved` (`Trans-Move-Whole`); moving a field makes it `PartiallyMoved({f})` (`Trans-Move-Field`); moving all fields collapses to `Moved` (`Trans-Partial-To-Moved`). Reassigning a `var` whose state is `Moved`/`PartiallyMoved` restores `Valid` (`Trans-Reassign`). The error transitions are:

- **(Trans-Moved-NoAccess)** — read or move of a `Moved` binding raises `Code(Trans-Moved-NoAccess)` → `E-MEM-3001`.
- **(Trans-Partial-NoAccess)** — read or move of a moved field of a `PartiallyMoved` binding → `E-MEM-3001`.
- **(Trans-Let-NoReassign)** — reassigning a `let` binding that is `Moved`/`PartiallyMoved` raises `Code(Trans-Let-NoReassign)` → `E-MEM-3006`.

At control-flow merge points, `JoinState` joins states (any-moved wins; partials union their field sets); `JoinBindInfo` requires the movability, mutability, and responsibility components to agree, and `JoinScope_B`/`Join_B` require identical domains. Permission states join with `JoinPermState` (both `Active` → `Active`, otherwise `Inactive`).

#### 21.3.4 Procedure Entry State

At procedure entry, the binding store is seeded from module statics and parameters:

```text
𝔅_global = IntroAll_B(PushScope_B(𝔅), StaticBindMap(Project(Γ), m))
𝔅_proc   = IntroAll_B(PushScope_B(𝔅_global), ParamBindMap(params))
```

A `move` parameter is introduced as `⟨Valid, mov, let, resp⟩` (movable and responsible); a non-`move` parameter as `⟨Valid, immov, let, alias⟩` (an alias, immovable) — see `ParamMov`/`ParamResp`. The `unique` parameters and `unique` statics start with their permission `Active` (`Init_Π`). `BindCheck` runs the binding judgment over the body; `ProcBindCheck`/`MethodBindCheck`/`StateMethodBindCheck`/`TransitionBindCheck` wire this into procedures, methods, state methods, and transitions.

This is why a non-`move` capability parameter behaves as a borrow: `procedure greet(io: $IO, ...)` takes `io` as an alias, so the caller retains its `$IO` and may keep using it after the call.

### 21.4 Regions, Frames, and Provenance (§6.4)

Ultraviolet's allocation model is **arena-based**. Beyond stack locals and the global allocator, you may open a *region*: a scoped arena into which you bulk-allocate with `new` for the current scoped region or with a `Region@Active` handle's `~>alloc` method for an explicit target. Regions are reclaimed wholesale at scope exit. *Frames* carve sub-lifetimes inside a region. **Provenance** is the static lifetime ordering that prevents a shorter-lived value from escaping into longer-lived storage.

#### 21.4.1 Region Options

```text
RegionOptionsFields = [
  ⟨⊥, `public`, false, `stack_size`, TypePrim("usize"), Literal(IntLiteral(0)), ⊥, ⊥⟩,
  ⟨⊥, `public`, false, `name`, TypeString(⊥), Literal(StringLiteral("")), ⊥, ⊥⟩
]
```

`RegionOptions` is a built-in `public` record with fields `stack_size: usize` (default `0`) and `name: string` (default empty). `RegionPrealloc(opts) = opts.stack_size`; `NoPrealloc(opts) ⇔ stack_size = 0`.

#### 21.4.2 The `region` Statement, the `frame` Statement, and Region Allocation

```ebnf
region_stmt  ::= "region" region_opts? region_alias? block_expr
region_opts  ::= "(" expression ")"
region_alias ::= "as" identifier
frame_stmt   ::= "frame" block_expr | identifier "." "frame" block_expr
```

Allocation uses `new value` for the current scoped region and the ordinary
modal method-call syntax `region_handle~>alloc(value)` for an explicit target.
`^` is only the bitwise XOR operator.

A `region` statement opens an arena for the duration of its block. The optional `(expr)` supplies a `RegionOptions` value (defaulting to `RegionOptions { }` via `RegionOptsExpr(⊥) = Call(Identifier(RegionOptions), [])`); the optional `as name` binds a `unique Region@Active` handle. Typing (**T-RegionStmt**, §18.7.4): the options expression checks against `RegionOptions`, the region binding is introduced as `unique Region@Active` (`RegionBind`), and the body is checked under it. If `as` is omitted, the region binding is *synthetic*: it MUST NOT be introduced by name resolution and MUST NOT be referenced by user code except as the implicit target of `new`. Code that must name a region explicitly uses `as`.

**Allocation yields the value's own type, not a pointer.** `new value` allocates into the current scoped region; `r~>alloc(value)` allocates into region `r`. Both forms yield a value of the *same type* `T` as `value`, living in that region (`T-New-CurrentRegion` or `T-Region-Alloc-Method`). The allocated value carries region provenance `π_Region(tag)` but its *type* is just `T` — do not annotate it as `Ptr<T>`.

```ultraviolet
procedure buildScene(io: $IO) -> Outcome<(), IoError> {
    region {
        let a := new Node { id: 0u32 }
        let b := new Node { id: 1u32 }

        // `a` and `b` are `Node` values living in `arena`; reclaimed in bulk at block end.
        io~>write_stdout("scene built\n")?
    }
    // Here, every allocation made into the region has been released.
    return Outcome::Value(())
}
```

The arena allocations bind with `:=` (immovable, `immov`): a region value is anchored to its arena and is not meant to be moved out.

A `frame` carves a *sub-arena* of an active region. `frame { ... }` targets the innermost active region; `r.frame { ... }` targets the named region `r`. On entry it records the arena *mark* (`FrameMark`); on exit it resets the arena back to that mark (`ResetArena`), so frame-local allocations are freed while earlier region allocations survive. Typing (**T-FrameStmt-Implicit/Explicit**, §18.8.4): `frame { ... }` with no active region in scope is `E-MEM-1207` (`Frame-NoActiveRegion-Err`); `r.frame { ... }` where `r` is not `Region@Active` is `E-MEM-1208` (`Frame-Target-NotActive-Err`).

```ultraviolet
procedure processBatches(io: $IO) -> Outcome<(), IoError> {
    region as arena {
        let shared := arena~>alloc(Config { batch_size: 256u32 })

        loop i: usize in 0usize..4usize {
            arena.frame {
                // Scratch allocations local to this batch:
                let scratch := new Buffer { len: 0usize }
                io~>write_stdout("batch done\n")?
            }
            // `scratch` reclaimed here; `shared` survives.
        }
    }
    return Outcome::Value(())
}
```

#### 21.4.3 Provenance Tags and Lifetime Order

Every value carries a provenance tag:

```text
π ::= π_Global | π_Stack(S) | π_Heap | π_Region(r) | ⊥
```

The lifetime order `π_1 < π_2` reads "`π_1` is shorter-lived than `π_2`":

```text
π_Region(r_inner) < π_Region(r_outer)   (when r_inner nests in r_outer)
π_Region(r)       < π_Stack(S)
π_Stack(S)        < π_Heap
π_Heap            < π_Global
π_Global          < ⊥
```

`≤` is the reflexive-transitive closure. `JoinProv(π_1, π_2)` is the shorter of two ordered tags (`⊥` if incomparable). The escape check is:

```text
EscapeOk(π_e, π_x) ⇔ ¬(π_e < π_x)
```

A value with provenance `π_e` may be stored into a location with provenance `π_x` only if `π_e` is *not* shorter-lived than `π_x`. Storing a region-local value into a stack or global location — where it would outlive its arena — is rejected with `E-MEM-3020`. This is consumed by assignment (`AssignProvOk`), closures (`ClosureEscapeCheck`), and async creation.

Two key provenance facts:

- **No general heap-escape conversion.** Heap provenance arises *only* from operations whose declared signatures explicitly accept a `$HeapAllocator` capability and return a heap-backed value. You cannot launder a stack value into the heap implicitly.
- **Closures.** A non-capturing closure has `π_Global` (`P-Closure-NonCapturing`); a capturing closure's provenance is the join of its captures' provenances (`P-Closure-Capturing`). An escaping closure that captures a local `shared` binding, or whose capture is shorter-lived than its target, is rejected (`P-Closure-Escape-Err`).

`region` statements and bindings of freshly created `Region@Active` values (such as `Region::new_scoped(...)`) introduce fresh region provenance tags (`FreshRegionExpr`). The `Region::alloc` operation yields a value with provenance `π_Region(tag)` where `tag` is the receiver handle's region tag (**P-Region-Alloc-Method**).

#### 21.4.4 The `Region` Modal Type and Its Procedures

`Region` is a modal type with states `@Active`, `@Frozen`, and `@Freed`, each carrying a `handle: usize` payload. `Region` is **not** `Bitcopy` (`¬ BitcopyType(TypePath(["Region"]))`). The region procedures (`RegionProcs`, with signatures from `RegionProcSig`):

| Procedure | Signature (receiver → result) |
| --- | --- |
| `Region::new_scoped(options: RegionOptions)` | → `unique Region@Active` |
| `Region::alloc(self: unique Region@Active, value: T)` | → `T` with provenance `π_Region(self)` |
| `Region::reset_unchecked(self: unique Region@Active)` | → `unique Region@Active` |
| `Region::freeze(self: unique Region@Active)` | → `unique Region@Frozen` |
| `Region::thaw(self: unique Region@Frozen)` | → `unique Region@Active` |
| `Region::free_unchecked(self: unique (Region@Active \| Region@Frozen))` | → `unique Region@Freed` |

`reset_unchecked` and `free_unchecked` require an `unsafe` context (`Region-Unchecked-Unsafe-Err`). After a reset or free, any dereference through a `Ptr<T>@Valid` whose address carries an inactive region tag MUST behave as `Expired` (see "Safe & Raw Pointers"); using a *non-pointer* value with provenance `π_Region(r)` after reset/free is outside conformance. `Region::free_unchecked` MUST be invoked exactly once on any region still `@Active`/`@Frozen` at scope exit — implementations MAY do this implicitly during `RegionStmt` cleanup, which is why the `region` statement frees its arena for you. The everyday surface for region allocation is `new value` for the current region and `region_handle~>alloc(value)` for an explicit target; `Region::new_scoped` is how you obtain a region value to thread as a `unique Region@Active` parameter.

```ultraviolet
procedure renderInto(scratch: unique Region@Active, io: $IO) -> Outcome<(), IoError> {
    // `scratch` is an explicitly-threaded region handle (e.g. from Region::new_scoped).
    let mesh := scratch~>alloc(Buffer { len: 1024usize })
    io~>write_stdout("rendered into scratch region\n")?
    return Outcome::Value(())
}
```

### 21.5 Dynamic Scope Stack, Bindings, and Region Runtime (§6.5)

§6.5 is the runtime counterpart to §6.3–§6.4: the concrete machinery that allocates, binds, reads, writes, and reclaims at execution time. Cleanup, unwinding, init/deinit, and interpreter entry are owned by Chapter 24; this section defines only the scope-stack, binding-store, and region-runtime operations they consume.

#### 21.5.1 Dynamic Scope Stack and Binding Store

```text
ScopeEntry = ⟨scope_id, cleanup, names, vals, states⟩
ScopeStack(σ) ∈ [ScopeEntry]
```

`PushScope_σ` pushes a fresh empty scope; `PopScope_σ` pops the top. `AppendCleanup` appends a cleanup item (e.g. a `DropBinding`) to the current scope's cleanup list. A binding is `⟨scope_id, bind_id, name⟩`; a binding's value is either a direct `Value` or `Alias(addr)` (a reference into memory). `LookupBind` finds the nearest scope defining the name and takes its most recent `bind_id`; `LookupVal` reads through aliases, statics, and module paths, but only if the owning module is not *poisoned* (`PoisonedModule`).

`BindVal(σ, x, v)` introduces a binding: it allocates a fresh `bind_id`, records value and `Valid` state, and — crucially — **if the binding is responsible (`resp`), appends a `DropBinding` cleanup item** so the value is dropped on scope exit; alias bindings register no cleanup. `BindPattern` binds a whole pattern by matching and binding each name in pattern order (`BindOrder`).

#### 21.5.2 Region Stack and Arenas

```text
RegionEntry  = ⟨tag, target, scope, mark_opt⟩
RegionStack(σ) ∈ [RegionEntry]
RegionArena(σ) : usize ⇀ [Addr]
AddrTags(σ)    : Addr ⇀ RuntimeTag        RuntimeTag = {RegionTag(tag), ScopeTag(sid)}
```

The region runtime maintains a stack of region entries and, per region target `r`, an arena: an ordered list of allocated addresses. The key operations:

- `RegionNew(σ, opts)` pushes a fresh scope, allocates a fresh arena, and pushes a region entry `⟨r, r, scope, ⊥⟩` — this is what the `region` statement runs on entry.
- `RegionOpen(σ, opts)` opens a region in the current scope (used by `Region::new_scoped`).
- `FrameEnter(σ, r)` pushes a scope, mints a fresh frame tag `F`, and records the current arena `mark = FrameMark(σ, r)` so the frame can reset back to it; it pushes `⟨F, r, scope, mark⟩`.
- `RegionAlloc(σ, r, v)` writes `v` to a fresh address, appends it to region `r`'s arena, and tags the address with `RegionTag(tag)`.
- `ArenaMark`/`ArenaResetTo`/`ArenaClear`/`ArenaRemove` take and reset arena marks; `RegionReset` clears the arena and re-tags so old pointers become inactive; `RegionFree` removes the arena and pops the region; `ResetArena` resets a frame to its mark and pops the frame scope.

**Region deallocation order (normative, §6.5.3):** `RegionRelease` and `FrameReset` MUST execute `CleanupScope` (running drops) *before* any `ArenaResetTo`/`ArenaRemove`. The arena reclamation operations `ArenaResetTo`, `ArenaClear`, and `ArenaRemove` MUST NOT invoke `Drop` — they only reclaim storage. Drops run first; raw arena bytes are freed second.

The region procedure relations tie the modal surface to the runtime: `RegionNewScoped` runs `RegionOpen`; `RegionAllocProc` runs `RegionAlloc`; `RegionResetProc` runs `RegionReset`; `RegionFreeProc` runs `RegionFree`; `RegionFreezeProc`/`RegionThawProc` re-tag the handle's state without touching storage.

#### 21.5.3 Address State and Pointer Expiry

`AddrTag(σ, addr)` classifies an address as a `ScopeTag(sid)` (a stack binding) or a `RegionTag(tag)` (an arena allocation). A tag is *active* iff its scope/region is still on the corresponding stack (`TagActive`). The dynamic address state:

```text
DynAddrState(σ, addr) =
  `Valid`    if AddrTag(σ, addr) = ⊥
  `Valid`    if AddrTag(σ, addr) = tag ≠ ⊥ ∧ TagActive(σ, tag)
  `Expired`  if AddrTag(σ, addr) = tag ≠ ⊥ ∧ ¬ TagActive(σ, tag)
```

This is the runtime basis for use-after-free safety: once a region (or scope) is gone, its addresses' tags are inactive, and a safe pointer through them reads as `Ptr@Expired` rather than dereferencing freed memory.

### 21.6 Runtime State and Memory Diagnostics (§6.6)

This section owns the binding-state, region/frame, provenance, and unsafe-runtime diagnostics consumed by Chapters 6, 16, and 18. Every code below is a compile-time error:

| Code | Condition |
| --- | --- |
| `E-MEM-1206` | Named region not found for allocation |
| `E-MEM-1207` | `frame` used with no active region in scope (`Frame-NoActiveRegion-Err`) |
| `E-MEM-1208` | `r.frame` target is not in `Region@Active` state (`Frame-Target-NotActive-Err`) |
| `E-MEM-3001` | Read or move of a binding in `Moved`/`PartiallyMoved` state (`Trans-Moved-NoAccess`, `Trans-Partial-NoAccess`, `B-Closure-RefCapture-Moved-Err`) |
| `E-MEM-3003` | Reassignment of an immutable binding |
| `E-MEM-3004` | Partial move from a binding without `unique` permission |
| `E-MEM-3005` | Explicit call to a `drop` method with a destructor signature |
| `E-MEM-3006` | Attempt to move from an immovable (`:=`) binding (`Trans-Let-NoReassign`, `B-Closure-MoveCapture-Immovable-Err`) |
| `E-MEM-3007` | `unique` binding from a place expression requires explicit `move` (`B-LetVar-UniqueNonMove-Err`) |
| `E-MEM-3020` | Value with shorter-lived provenance escapes to longer-lived location |
| `E-MEM-3021` | `new` allocation used with no active region in scope |
| `E-MEM-3030` | Unsafe operation outside `unsafe` block (`AllocRaw-Unsafe-Err`, `DeallocRaw-Unsafe-Err`, `Region-Unchecked-Unsafe-Err`, `Transmute-Unsafe-Err`) |

Note also the capability-flow rejection of §6.1.2 (NAA-3): a call whose target requires a capability the caller does not effectively hold is rejected. Combined with the WF rule for `$Class` types (rejected when the class path is undefined or non-dispatchable, §14.6.4) and `LookupClassMethod-NotFound` (an unknown method on a `$Class`), these are the static guardrails for the entire authority model.

### Idioms & Best Practices

- **Thread the narrowest capability.** The style guide is explicit (§"Capability Passing"): "Pass only the exact capabilities a procedure or method uses." Take `io: $IO`, not `ctx: Context`, in a helper that only writes output. The capability-flow rule then *proves* the helper cannot do anything else. Reach for `Context` only at the boundary (`main`, top-level coordinators) where you genuinely fan out to multiple subsystems; for a recurring subsystem boundary, define a narrow projected context record (a valid `ContextBundleType`).
- **Attenuate before you delegate.** Before handing a capability to a subsystem you do not fully trust, narrow it: `ctx.io~>restrict("data")`, `ctx.net~>restrict_to_host("api.example.com")`, `ctx.heap~>with_quota(limit)`. Attenuation is monotone, so the narrowed handle is a hard upper bound on what the callee can do.
- **`main` is `public ... -> i32`.** The entry procedure is exactly `public procedure main(ctx: Context) -> i32` (or a context-bundle record in place of `Context`). The name `main` is the language-mandated exception to camelCase naming (style guide §"Naming Exceptions"); the file may be `Main.uv`, but the procedure stays `main`.
- **Use `~>` for capabilities, modal state methods, and transitions; `.` for fields.** `ctx.io` is a field read (`.`); `io~>write_stdout(...)`, `file~>read_all()`, and `file~>close()` are `~>` calls. Mixing them up will not compile.
- **`Outcome` is a two-variant enum, not a modal.** Match it with enum patterns — `if r is Outcome::Value(x) { ... }` and `if r is Outcome::Error(e) { ... }` — and construct it with `Outcome::Value(v)` / `Outcome::Error(e)`, or let it be introduced implicitly in an `Outcome`-typed context. The older modal record-literal forms (`@Value { value: v }` / `@Error { error: e }`) are not used for `Outcome`.
- **Propagate with `?`, narrow at the altitude that decides the exit code.** In a fallible procedure returning `Outcome<_, IoError>`, write `io~>write_stdout(...)?`. At `main` (which returns `i32`), pattern-match the final `Outcome` (or `let _ = ...` to discard intentionally) and choose the status code.
- **The `$IO` openers return `Outcome`, matched like any other `Outcome`.** `open_read`, `open_write`, `open_append`, `create_write`, `open_dir`, and `kind` return `Outcome<T, IoError>` (not a bare union). Test the error with `if x is Outcome::Error(e) { ... }` and the success with `if x is Outcome::Value(h) { ... }`.
- **Prefer `region`/`frame` for bulk, short-lived allocation.** When you build a transient graph, parse into temporaries, or process per-iteration scratch, open a `region` (and a `frame` per iteration). Bind arena allocations with `:=`. Reclamation is wholesale and deterministic at block exit — drops run, then arena bytes are freed — with no per-object bookkeeping.
- **An arena allocation has the value's own type.** `new Node { ... }` and `arena~>alloc(Node { ... })` are `Node` values carrying region provenance; do not annotate them `Ptr<Node>`. If you need a pointer into the arena, take its address with `&` separately.
- **Right clock for the job.** `time~>monotonic()` for durations/benchmarks (it is monotone within its domain); `time~>wall()` then `now_utc()` for calendar time. Never measure elapsed time with the wall clock, and keep `elapsed(start, end)` instants in the same authorized clock domain.

### Pitfalls & Diagnostics

- **Trying to perform an effect without a capability.** There is no ambient `print`. A procedure with no `$IO` (directly or via `Context`) that attempts `write_stdout` is rejected by NAA-3 (`CapReq(d_tgt) ⊄ EffectiveCapReq(d_src)`). The fix is to add the capability to the signature and thread it from `main`.
- **Writing modal `@Value`/`@Error` forms for `Outcome`.** `Outcome` is a two-variant enum, not a modal: the modal record-literal (`Outcome@Value { value: v }`) and bare `@Value`/`@Error` pattern forms do not apply. Construct with `Outcome::Value(v)` / `Outcome::Error(e)` and match with the same enum patterns.
- **Brace-less `if` bodies.** `if_tail` requires a `block_expr`: `if cond return 1` is a parse error. Always use braces — `if cond { return 1 }`.
- **Mistyping an arena allocation as a pointer.** `let a: Ptr<Node>@Valid = new Node { ... }` is a type error: `new Node { ... }` has type `Node`. Bind it as a `Node` (idiomatically with `:=`).
- **Letting a region-local value escape.** Returning, or assigning into a longer-lived location, a value allocated with `new` or `Region@Active~>alloc` triggers `E-MEM-3020` — its provenance `π_Region(r)` is shorter-lived than the destination. Keep arena values inside the arena's scope, or allocate them somewhere longer-lived (e.g. through `$HeapAllocator`).
- **Using `new` without an active region.** `new value` requires an active `region` or `frame` scope and otherwise emits `E-MEM-3021`. Open a scoped region first, or use an explicit `Region@Active` handle with `r~>alloc(value)`.
- **`frame` with no active region / on a non-active region.** `frame { ... }` outside any region is `E-MEM-1207`; `r.frame { ... }` where `r` is not `Region@Active` (e.g. it was frozen or freed) is `E-MEM-1208`.
- **Calling `reset_unchecked`/`free_unchecked`/`alloc_raw`/`dealloc_raw` outside `unsafe`.** These require an `unsafe` block (`E-MEM-3030`, via `AllocRaw-Unsafe-Err`/`DeallocRaw-Unsafe-Err`/`Region-Unchecked-Unsafe-Err`). The safe path is to let the `region` statement reclaim the arena implicitly; use the unchecked procedures only as deliberate boundary tools.
- **Use-after-reset.** After `Region::reset_unchecked`/`free_unchecked`, a `Ptr<T>@Valid` into the reclaimed arena reads as `Ptr@Expired` (its region tag is now inactive). Using a *non-pointer* value with `π_Region(r)` provenance after reset/free is *outside conformance* — there is no defined behavior to rely on.
- **Moved-binding access.** Reading or moving a binding after it (or the relevant field) has been moved is `E-MEM-3001`; reassigning a moved `let` is `E-MEM-3006`. Use `var` if you intend to reassign, and remember a non-`move` capability parameter is an alias you do not consume.
- **Assuming attenuation widens.** A restricted `$IO` cannot reach paths outside its base; `restrict` never grants more than the parent. Likewise a `with_quota` allocator rejects allocations past its budget with `AllocationError::QuotaExceeded`. Do not attempt to "un-restrict" — derive a fresh handle from a parent that still holds the broader authority, and never drop a parent while a derived child is live (that is ill-formed).
