## 28. Program Lifecycle, Drop, ABI & Runtime

This chapter describes what happens to an Ultraviolet program *around* the code you write: how it starts, how its module-scope state is brought up and torn down, how values are destroyed when scopes exit or panics unwind, what runtime services the generated code calls into, and the layout/ABI/linkage facts you need when interoperating with foreign code. The deep compiler-internal codegen (SSA construction, branch lowering, GEP emission) is summarized at reference level; everything that is *observable* from a correct program — initialization order, drop order, the `main` contract, panic propagation, calling conventions, and symbol names — is covered in full.

The owning specification sections are §24 (Common Lowering, Program Lifecycle, and Backend) and §1.6 (Target and ABI Assumptions). Related handbook chapters: **Procedures & Contracts** (the `main` signature and `@entry`/`@result` intrinsics), **Records, Enums & Modals** (the `drop` method and `Drop` foundational class), **Statements & Control Flow** (`defer`, `region`, scope exit), **Capabilities & the Context** (the `Context` bundle threaded into `main`), and **FFI & Extern** (`extern` blocks, `#export`, `#host_export`).

### 28.1 The Target Profile

Everything in this chapter is parameterized by a single value resolved once per compilation: the **selected target profile**.

```text
TargetProfile = {`x86_64-sysv`, `x86_64-win64`, `aarch64-aapcs64`, `aarch64-darwin`}
SelectedTargetProfile ∈ TargetProfile
```

The profile is resolved in this order (§1.6):

1. the explicit CLI target-profile override, if provided;
2. otherwise `toolchain.target_profile` from `Ultraviolet.toml`, if provided;
3. otherwise the compilation invocation is ill-formed (diagnostic `E-PRJ-0112`).

A conforming implementation MUST NOT silently infer the profile from the host platform. The profile fixes the architecture, object format, executable suffix, linker, runtime library name, LLVM triple, and data layout. Key derived facts:

```text
TargetArch(`x86_64-sysv`)     = `x86_64`        TargetArch(`aarch64-darwin`) = `aarch64`
Endianness                    = Little
PtrSizeBytes                  = PtrSize          (= 8)

ObjFormatOf(`x86_64-sysv`)     = "ELF"    ExeSuffix(`x86_64-sysv`)     = ""
ObjFormatOf(`x86_64-win64`)    = "COFF"   ExeSuffix(`x86_64-win64`)    = ".exe"
ObjFormatOf(`aarch64-aapcs64`) = "ELF"    ExeSuffix(`aarch64-aapcs64`) = ""
ObjFormatOf(`aarch64-darwin`)  = "MachO"  ExeSuffix(`aarch64-darwin`)  = ""

LLVMTripleOf(`x86_64-win64`)   = "x86_64-pc-windows-msvc"
RuntimeLibNameFor(`x86_64-sysv`)  = "UltravioletRT.a"
RuntimeLibNameFor(`x86_64-win64`) = "UltravioletRT.lib"
```

Every program links against the Ultraviolet runtime library (`UltravioletRT.*`) for the selected profile; this library provides the runtime symbols enumerated in §28.7.

### 28.2 Layout & ABI Essentials

You need layout facts whenever you write `extern` declarations, pass aggregates across an FFI boundary, or reason about `sizeof`/`alignof`. Layout is little-endian on every profile.

#### 28.2.1 Primitive Sizes and Alignments

Sizes and alignments of primitives are fixed across all profiles (§24.2.1):

```text
PtrSize = 8        PtrAlign = 8

       size  align              size  align
i8       1     1     u8           1     1
i16      2     2     u16          2     2
i32      4     4     u32          4     4
i64      8     8     u64          8     8
i128    16    16     u128        16    16
f16      2     2     f32          4     4      f64    8    8
bool     1     1     char         4     4      (UTF-32 scalar)
usize    8     8     isize        8     8      (= PtrSize / PtrAlign)
()       0     1     !            0     1
```

A `bool` is valid only as the byte `0x00` (`false`) or `0x01` (`true`); a `char` is valid only as a 4-byte little-endian Unicode scalar value (§24.2.1, **Valid-Bool**, **Valid-Char**). Writing any other bit pattern into a `bool` or `char` through unsafe means is undefined. The never type `!` has no valid value at all (`ValidValue(!, bits) ⇔ false`).

`sizeof`, `alignof`, and `layout` are defined for primitives, permission-qualified types (a permission never changes layout), safe pointers, raw pointers, and function pointers:

```text
sizeof(TypePerm(p, T))   = sizeof(T)       alignof(TypePerm(p, T))   = alignof(T)
sizeof(TypePtr(U, s))    = PtrSize         alignof(TypePtr(U, s))    = PtrAlign
sizeof(TypeRawPtr(q, U)) = PtrSize         alignof(TypeRawPtr(q, U)) = PtrAlign
sizeof(TypeFunc(ps, R))  = PtrSize         alignof(TypeFunc(ps, R))  = PtrAlign
```

A permission qualifier (`const`, `unique`, `shared`) is purely a static discipline: `layout(unique T) = layout(T)`. This is why moving a value or aliasing it never changes its representation. Slices are `{ptr, usize}` (`2 × PtrSize`, `PtrAlign`).

#### 28.2.2 Default Calling Convention

The default calling convention is C:

```text
CallConvDefault = `C`
CallingConvention = { `C`, `C-unwind`, `system`, `stdcall`, `fastcall`, `vectorcall` }
```

The set of ABI strings usable on an `extern` block (or `#export`/`#host_export`) is:

```text
ExternAbiSet = { `"C"`, `"C-unwind"`, `"system"`, `"stdcall"`, `"fastcall"`, `"vectorcall"` }
AbiToConvention("C") = `C`     AbiToConvention("C-unwind") = `C-unwind`     …
```

If an `extern` block has no ABI string, the ABI defaults to `"C"` (§24.3.1, `ExternAbiName(⊥) = "C"`). The per-profile register assignment, callee-saved set, and stack alignment are given by `ConventionLayout`; the only field that varies for `C-unwind` is `unwind_support := true`. On every profile the default-C convention reports `panic_passing = OutParam`, which is the mechanism described in §28.4.5.

#### 28.2.3 Parameter and Return Passing

Ultraviolet passes parameters and returns by value, by reference, or via a struct-return out-pointer. The pass kind is computed from size and alignment:

```text
PassKind   = {`ByValue`, `ByRef`, `SRet`}
ByValMax   = 2 × PtrSize        (= 16)
ByValAlign = PtrAlign           (= 8)
ByValOk(T) ⇔ sizeof(T) ≤ 16 ∧ alignof(T) ≤ 8
```

For *ordinary* (Ultraviolet-to-Ultraviolet) calls, `ABIParam` drives parameter passing, and it always chooses **`ByRef`** for source parameters:

- A parameter with no mode (an implicit alias) is passed **`ByRef`** — the callee receives a pointer (§24.2.5, **ABI-Param-ByRef-Alias**).
- A parameter with mode `move` is also passed **`ByRef`** (§24.2.5, **ABI-Param-ByRef-Move**).

The only parameter modes the surface grammar admits are no mode and `move` (`param_mode ::= "move"`).

Returns:

- A zero-sized return or one satisfying `ByValOk` is returned **`ByValue`** (§24.2.5, **ABI-Ret-ByValue**).
- Any larger return is returned via **`SRet`** — the caller allocates the result slot and passes a hidden out-pointer, and the LLVM return becomes `void` (§24.2.5, **ABI-Ret-ByRef**; §24.7.10, **LLVMCall-SRet**).

For *foreign* boundaries (`extern` procedures, exported `#export`/`#host_export` procedures, whose signatures carry no Ultraviolet mode information), `ForeignABIParam` is used instead: a parameter is passed `ByValue` when zero-sized or `ByValOk`, otherwise `ByRef` (§24.2.5, **ABI-ForeignParam-ByValue/ByRef**). This is the rule that governs what your `extern` signature actually looks like on the wire, and why small aggregates cross by value while large ones cross by reference. The return-passing rule (`ABIRet`) is shared between ordinary and foreign calls.

```ultraviolet
// A small record (<= 16 bytes, align <= 8) crosses a C boundary by value;
// a larger one would be passed by reference automatically by ForeignABIParam.
#layout(C)
public record Point {
    public x: f64
    public y: f64
}

extern "C" {
    /// Foreign routine taking a 16-byte aggregate by value. The crossing type
    /// must be FfiSafe; ForeignABIParam(Point) = ByValue because Point is
    /// 16 bytes with alignment 8.
    public procedure plot_point(p: Point) -> ()
}
```

### 28.3 Symbols, Mangling & Linkage

Symbol names matter to you in two situations: when foreign code must find your procedure, and when you must name a foreign symbol exactly.

#### 28.3.1 How names are produced

By default, an item's symbol is its **scoped mangled name** — a deterministic FNV-1a hash-based encoding of its full module path (`ScopedSym(item) = PathSig(ItemPath(item))`). The `#mangle` and `#export` attributes change this (§24.3.1, `LinkName`):

- `#mangle(none)` — use the raw, unmangled item name as the symbol.
- `#mangle("custom_symbol")` — use the given literal string as the symbol.
- `#export("C")` — keep the scoped (mangled) symbol but mark the item externally visible at the chosen ABI. (When `#mangle` is also present, `#mangle` wins and fixes the literal symbol.)
- An `extern` procedure whose ABI is `"C"` or `"C-unwind"` uses its **raw name** as the symbol (so `extern "C" { procedure malloc(...) }` links to `malloc`).

The exact `LinkName` priority is: a `#mangle` attribute (none → raw item name; string → that string) takes precedence; otherwise an `extern` `"C"`/`"C-unwind"` procedure uses its raw name; otherwise (with or without `#export`) the scoped mangled symbol is used.

`main` is special-cased: its `LinkName` is computed like any procedure, but the entry symbol the OS calls is always `PathSig(["main"])` (§28.4).

The attribute spellings are exactly `#mangle(none)`, `#mangle("...")`, `#export("...")`, and `#host_export("...")` (`mangle_attribute`, `export_attribute`, `host_export_attribute` in Appendix B.8).

```ultraviolet
// Expose an Ultraviolet routine to C under an exact, stable symbol name.
// #mangle takes precedence over #export, so the emitted symbol is the literal
// "uv_compute_checksum".
#export("C")
#mangle("uv_compute_checksum")
public procedure computeChecksum(data: bytes@View) -> u32 {
    return hashBytes(data)
}
```

#### 28.3.2 Linkage

Linkage is `external` or `internal` (§24.3.2). The rules you care about:

- `public` and `internal` procedures, statics, methods, class methods (with bodies), state methods, and transitions get **`external`** linkage.
- `private` items get **`internal`** linkage.
- `extern` procedures are **`external`** (they are imports).
- The entry symbol `main` (`EntrySym`) is **`external`**.
- Compiler-generated symbols — module init/deinit functions, vtables, literal data, drop glue, the poison flags, and the runtime/panic symbols — are **`internal`**.

The practical consequence: only `public`/`internal` (and exported) items are visible to other link units; `private` is genuinely link-local.

### 28.4 Initialization & Program Lifecycle

This is the developer-relevant heart of the chapter: how a program starts, runs `main`, and shuts down.

#### 28.4.1 The `main` entry contract

An executable project must define exactly one `main`. Its signature is fixed (§15, `MainSigOk`):

```text
MainSigOk(d) ⇔ d = ProcedureDecl(_, vis, `main`, _, _, params, ret_opt, _, _, _, _)
             ∧ vis = `public`
             ∧ params = [⟨mode, name, ty⟩]
             ∧ mode ∈ {⊥, `move`}
             ∧ ContextBundleType(StripPerm(ty))
             ∧ ret_opt = TypePrim("i32")
```

In words, `main` MUST:

- be `public`;
- be non-generic (no type parameters);
- take **exactly one** parameter, in mode `move` or no mode, whose type is a **context-bundle type** — either the built-in `Context` or a record all of whose fields are context-bundle field types or nested context bundles (§13, `ContextBundleType`). This is the capability bundle the runtime constructs and hands you; see the **Capabilities & the Context** chapter;
- return `i32`.

The valid context-bundle field names and their dynamic capability types are fixed (§13, `ContextBundleFieldType`): `io: $IO`, `net: $Network`, `heap: $HeapAllocator`, `sys: $System`, `reactor: $Reactor`, `time: $Time`, and the three execution domains `cpu`/`gpu`/`inline: $ExecutionDomain`. A projected context record may declare any subset of these fields under their exact field names with the matching `$`-prefixed dynamic type, or nest another context-bundle record.

The corresponding diagnostics (§15.10) are:

| Code | Condition |
| ---- | --------- |
| `E-MOD-2430` | Multiple `main` procedures defined (`Main-Multiple`) |
| `E-MOD-2431` | Invalid `main` signature (`Main-Signature-Err`) |
| `E-MOD-2432` | `main` is generic (has type parameters) (`Main-Generic-Err`) |
| `E-MOD-2434` | Missing `main` procedure (`Main-Missing`) |

```ultraviolet
/// Program entry point. Receives the full capability context and returns a
/// process exit code. `ctx.io` is a `$IO` capability value; capability methods
/// dispatch with the `~>` operator.
public procedure main(ctx: Context) -> i32 {
    ctx.io~>write_stdout("hello\n")
    return 0
}
```

You may also narrow the parameter to a projected context record instead of taking the whole `Context`. Every field name and type must be a valid context-bundle field:

```ultraviolet
/// A narrow boundary context exposing only the capabilities main needs.
/// Field names and `$`-prefixed dynamic types must match ContextBundleFieldType.
public record AppContext {
    public io: $IO
    public time: $Time
}

public procedure main(ctx: AppContext) -> i32 {
    // `ctx.time` is `$Time`; `~>monotonic()` yields `$MonotonicTime`,
    // and `~>now()` yields a MonotonicInstant.
    let started = ctx.time~>monotonic()~>now()
    ctx.io~>write_stdout("starting\n")
    return 0
}
```

#### 28.4.2 The entry stub and startup sequence

The backend emits an entry stub at symbol `main` (i.e. `EntrySym ⇓ PathSig(["main"])`), with type `() -> i32` (§24.4.3, **EntryStub-Decl**). Its specified behavior (`EntryStubSpec`) is the canonical startup sequence:

1. Call the runtime **context initializer** `ContextInitSym` (`= ultraviolet::runtime::context_init`) to obtain the platform context value `ctx`.
2. Build the actual `main` argument from `ctx` by `ContextBundleBuild(StripPerm(MainArgType(d)), ctx)` — this projects the context into whatever bundle type `main` declared (the whole `Context`, or a narrow record).
3. Initialize the per-process **panic record** to "no panic pending" (`PanicRecordInit`: `⟨false, 0⟩`).
4. Run **static initialization** (`Init(G_e, σ)`) — described in §28.4.3.
5. Call user `main` with `[arg, __panic]` (the panic out-parameter is threaded automatically; see §28.4.5).
6. Inspect the panic record:
   - If a panic is pending (`⟨true, c⟩`), call the runtime `panic` symbol with code `c`; the process diverges. Static deinitialization is **not** run on this path.
   - If no panic is pending (`⟨false, c⟩`), run **static deinitialization** (`EmitDeinitPlan`) and the stub yields `main`'s returned `i32` as the process exit code.

The platform-specific details — argv, current directory, path encoding — are isolated behind the runtime host boundary (`ProcessInvocationNormalization`); source code observes them only through the normalized `System` capability methods on the context, never through raw `argc`/`argv`.

#### 28.4.3 Static globals and initialization order

Module-scope `static` declarations are the program's globals. Their grammar (Appendix B.6):

```ebnf
static_decl  ::= attribute_list? visibility? ("let" | "var") binding_decl
binding_decl ::= pattern (":" type)? binding_op expression
binding_op   ::= "=" | ":="
```

A `let` static whose initializer is a constant literal is emitted as read-only initialized data (`GlobalConst`); a `var` static, or a `let` whose initializer is not a compile-time constant literal, is emitted as zero-initialized storage (`GlobalZero`) that is filled in at runtime during static init (§24.4.1, **Emit-Static-Const**, **Emit-Static-Init**).

Runtime static initialization runs **before** `main`. Its order is a topological sort of the eager static-initialization dependency graph `G_e` (supplied by §11.5.4), with ties broken by source module order (§24.4.2):

```text
InitOrder(G_e) = L  ⇔  Topo(G_e) ⇓ L          (topological, source-module-order tie-break)
InitPlan(G_e)  = ++_{m ∈ InitOrder(G_e)} InitList(m)
DeinitOrder(G_e) = rev(InitOrder(G_e))
```

So: statics initialize in dependency order, and tear down in the **exact reverse** order at shutdown. A cycle in `G_e` is a compile-time error (**Topo-Cycle**).

```ultraviolet
// `let` with a constant literal -> read-only data, no runtime initializer.
public let MAX_RETRIES: u32 = 5

// `let` with a non-constant initializer -> zero storage, filled during static
// init. Private module-scope statics use the `_SCREAMING_SNAKE` form.
let _DEFAULT_TABLE: LookupTable = buildDefaultTable()
```

Per the style guide, name module-scope and static values `SCREAMING_SNAKE` (private ones `_SCREAMING_SNAKE`), prefer immutable statics, and never expose public mutable module-scope state.

#### 28.4.4 Initialization panics and poisoning

If a static initializer **panics**, the program does not silently continue. The init driver transitions to `InitPanic` and the module that panicked — together with every module that (eagerly) depends on it — is added to a **poison set** (§24.4.2, **Init-Panic**; §24.7.13):

```text
PoisonSet(m) = {m} ∪ {x | Reachable(x, m, E_val^{eager})}
```

Each module has a `bool` poison flag (`PoisonFlag(m) = ultraviolet::runtime::poison::…`). When an init panic occurs, those flags are set (`SetPoison`), and any later attempt to read a static, call a procedure, or construct a record in a poisoned module triggers a `CheckPoison` that raises an `InitPanic(m)` panic (code `0x000A`). Crucially, partial init is cleaned up correctly: if a module's init panics after only a strict prefix of its responsible static bindings completed, only that completed prefix is dropped, in reverse, and no later module is initialized or deinitialized (§24.4.4, `InitPanicHandle`). This guarantees you never observe a half-constructed global as if it were whole.

#### 28.4.5 The panic-out parameter

Ultraviolet has no zero-cost exceptions on the default C convention; instead, panics are reported through a hidden out-parameter. The compiler appends a `__panic` parameter to (almost) every procedure signature it lowers:

```text
PanicRecordFields = [⟨`panic`, bool⟩, ⟨`code`, u32⟩]
PanicOutType = TypeRawPtr(`mut`, PanicRecord)
PanicOutName = "__panic"
PanicOutParam = ⟨`move`, "__panic", PanicOutType⟩
```

A few callees do **not** get a `__panic` parameter (`NeedsPanicOut(callee) ⇔ callee ≠ RecordCtor(_) ∧ callee ≠ EntrySym ∧ RuntimeSig(callee) undefined`): record constructors, the entry symbol, and runtime symbols. After every ordinary call, lowered code performs a `PanicCheck`: if the record now says `⟨true, c⟩`, the caller itself enters the `Panic` control state and unwinds (running cleanup — §28.5). This is entirely automatic; you never write `__panic` yourself. You only observe its *effect*: a panic anywhere below you runs your `defer` blocks and drops on the way out, and ultimately reaches the entry stub, which converts a still-pending panic into the runtime `panic` call.

Panic codes are fixed (§24.5.2). The full table:

```text
ErrorExpr      0x0001   Bounds         0x0006   ContractPre    0x000B   ForeignPost  0x000F
ErrorStmt      0x0002   Cast           0x0007   ContractPost   0x000C   TypeInv      0x0010
DivZero        0x0003   NullDeref      0x0008   AsyncFailed    0x000D   LoopInv      0x0011
Overflow       0x0004   ExpiredDeref   0x0009   ForeignPre     0x000E   MatchFail    0x0012
Shift          0x0005   InitPanic      0x000A                           Other        0x00FF
```

#### 28.4.6 Interpreter and library lifecycles

The same lifecycle is defined for non-executable hosting modes (developer-relevant when you build a library):

- **Interpreter entrypoint** (§24.4.5): construct the context (`ContextInitSigma`), run `Init`, call `main`, then run `Deinit`. An init panic aborts before `main`; a `main` panic/abort surfaces as a control result; a deinit panic surfaces as a panic.
- **Shared-library images** (§24.4.4): a loaded image runs `LibraryImageInitSigma` (which runs full static `Init`) before user code is first callable through it, and runs `LibraryImageDestroySigma` (full `Deinit`) exactly once on unload. Each loaded image owns an independent copy of all static storage, poison flags, and the boundary panic record (`DistinctLibraryImageState`); distinct images never share that state.
- **Hosted-library sessions** (§24.4.4): `#host_export` procedures run inside an explicit session created by `HostSessionInitSigma` and destroyed by `HostSessionDestroySigma`. Each live session owns independent static state and its own panic record (`DistinctHostedState`); sessions MUST NOT be entered concurrently or reentrantly. The first parameter of a `#host_export` procedure is the context bundle (a `ContextBundleType`, passed in mode no-mode — `move` is rejected by `HostExport-Context-Move-Err`), just like `main`. At the foreign ABI, session handles are nonzero `usize` tokens.

### 28.5 Cleanup, Drop & Unwinding

Ultraviolet performs deterministic destruction at scope exit and during unwinding. There is no garbage collector and no nondeterministic finalization.

#### 28.5.1 What "drop" means

A type `T` is a **drop type** when it either is a built-in managed type or has a user `drop` method (§14.10):

```text
DropType(T) ⇔ BuiltinDropType(T) ∨ HasDropMethod(StripPerm(T))
BuiltinDropType(T) ⇔ T = string@Managed ∨ T = bytes@Managed
HasDropMethod(T) ⇔ T = TypePath(p) ∧ RecordDecl(p) = R ∧ ∃ m ∈ Methods(R).
                   MethodName(m) = `drop` ∧ Sig_T(T, m) = ⟨unique T, [], ()⟩
```

So a user destructor is a method named **`drop`** taking a `unique` self, no other parameters, and returning `()`. The `Drop` foundational class is satisfied exactly when `DropType(T)` holds (`Γ ⊢ T <: Drop ⇔ DropType(T)`), and a type may not be both `Bitcopy` and `Drop` — that combination is a compile-time conflict (§14.10, **BitcopyDrop-Conflict**). You require droppability in a generic bound with `<T <: Drop>`.

```ultraviolet
/// Owns a foreign handle and releases it deterministically on scope exit.
public record FileHandle {
    private _fd: i32

    /// Destructor: runs automatically at the final owning scope exit.
    /// The `~!` receiver is exactly `unique self`, the signature `drop` requires.
    procedure drop(~!) {
        closeFd(self._fd)
    }
}
```

Here `~!` is the receiver shorthand for `unique self` (`receiver_shorthand ::= "~" | "~!" | "~%"`) — exactly the signature `drop` requires.

#### 28.5.2 Drop order

At the final owning scope exit of a value (§14.10.5, §24.5.3, `DropValueOut`):

1. The type's own `drop` is invoked (if `DropType(T)`). For `string@Managed`/`bytes@Managed` this is the runtime `drop_managed` hook (`StringDropSym`/`BytesDropSym`); for a user record it is your `drop` method.
2. Then the value's **owned children are dropped in reverse construction order** (`DropChildren` walks record fields, tuple elements, array elements, union cases, and modal-state payloads back-to-front).
3. Finally the value's provenance/allocation domain is released (`ReleaseValue`).

A binding that has been **moved out** has transferred its cleanup responsibility and is skipped at its original scope (`DropAction-Moved`); a **partially moved** binding drops only the fields it still owns, excluding the moved fields (`DropAction-Partial`). For a non-drop type with no owned children and no domain storage, cleanup is a genuine no-op.

Within a scope, bindings are dropped in reverse order of their cleanup list (the driver always pops the last item: `CleanupList(scope) = rest ++ [item]`), so the last binding created is the first destroyed — the standard stack discipline.

#### 28.5.3 `defer`

A `defer` statement registers a block to run on scope exit, *after* the rest of the scope body, in LIFO order relative to other `defer`s and drops in that scope.

```ebnf
defer_stmt ::= "defer" block_expr
```

Semantics (§18.6): executing `defer { … }` appends the block to the current scope's cleanup list (`AppendCleanup(σ, DeferBlock(b))`); it does **not** run the block immediately. The block runs during scope exit, woven into the same reverse-order cleanup loop as drops (§24.5.4, **Cleanup-Step-Defer-Ok**).

Two static restrictions (§18.6.7):

- A `defer` block MUST have type `()` — `E-SEM-3151` (`Defer-NonUnit-Err`).
- A `defer` block MUST NOT contain non-local control flow (`return`, `break`, `continue` escaping the block) — `E-SEM-3152` (`Defer-NonLocal-Err`).

```ultraviolet
public procedure processBatch(io: $IO, path: string@View) -> i32 {
    let file = openOrPanic(io, path)
    defer {
        io~>write_stdout("batch finished\n")
    }
    // `file`'s drop and the deferred block both run on every exit path,
    // including a panic that unwinds through this scope. The deferred block
    // runs LIFO, after the body, and before `file` is dropped.
    return runJobs(file)
}
```

#### 28.5.4 The cleanup driver, panics during cleanup, and abort

Scope exit runs a small-step cleanup loop over the scope's cleanup items (`DropBinding`, `DropStatic`, `DeferBlock`), each in reverse order (§24.5.4). The loop carries a flag `c ∈ {ok, panic}`:

- A drop or deferred block that completes normally continues the loop, preserving `c`.
- A drop or deferred block that **panics** while `c = ok` sets `c := panic` and **continues** cleaning the remaining items (`Cleanup-Step-*-Panic`) — so a panic in one destructor does not skip the others.
- A drop or deferred block that panics while `c = panic` (a panic during cleanup that was already unwinding a panic, i.e. a *double panic*) transitions to **`Abort`** (`Cleanup-Step-*-Abort`) — the program terminates abnormally; no further cleanup runs.

Unwinding a stack of frames applies this per-frame: each frame's scope is cleaned (`Unwind-Step`), and a panic produced *by* cleanup escalates to `Abort` (`Unwind-Abort`). The lesson for destructor authors: a `drop` method (or `defer` block) that itself panics is recoverable once, but a destructor that panics *during* an in-progress panic aborts the process. Keep destructors infallible.

#### 28.5.5 Static teardown

Static deinitialization at normal shutdown drops every responsible static binding in **reverse global static order** (`DeinitList(P) = rev([DropStatic(path, name) | …])`, §24.4.2). It is driven by the same `Cleanup` machinery, so the same panic/abort rules apply (a deinit panic surfaces as a panic via `Deinit-Panic`). Static teardown runs only on the no-panic exit path (§28.4.2): if `main` left a panic pending, the process diverges through the runtime `panic` symbol and statics are not torn down.

### 28.6 Temporary Cleanup

A subtle but observable rule: temporaries created while evaluating a statement are dropped at the end of that statement, in reverse creation order, *before* control leaves on `return`/`break`/`continue` (§18.1.6):

```text
TempCleanupIR(s) = SeqIRList([EmitDrop(T_k, v_k), …, EmitDrop(T_1, v_1)])   (k temporaries)
```

For a control-flow statement, the temporary drops are emitted immediately before the transfer (e.g. `return e` lowers to: evaluate `e`, drop temporaries, then `ReturnIR(v)`). This is why a drop-typed temporary in an expression is released promptly and deterministically rather than lingering to end of block.

### 28.7 The Runtime Interface

Generated code calls a fixed set of runtime symbols, all `internal`-linked and resolved from `UltravioletRT.*`. You never call these by name in source — you reach them through language features (capability methods, managed strings/bytes, regions, async) — but knowing they exist explains the linkage and the required runtime library.

The runtime symbol surface (§24.6.3, `RuntimeSyms`) is:

```text
RuntimeSyms = {PanicSym, StringDropSym, BytesDropSym, ContextInitSym}
            ∪ {BuiltinModalSym(proc) | proc ∈ dom(BuiltinModalSymMap)}
            ∪ {RegionAddrIsActiveSym, RegionAddrTagFromSym}
            ∪ {BuiltinSym(method) | method ∈ BuiltinMethods}
```

In developer terms:

- **Panic & context:** `ultraviolet::runtime::panic` (`PanicSym`), `ultraviolet::runtime::context_init` (`ContextInitSym`).
- **Managed-type drop hooks:** `ultraviolet::runtime::string::drop_managed` (`StringDropSym`), `ultraviolet::runtime::bytes::drop_managed` (`BytesDropSym`) — these are the destructors invoked for `string@Managed`/`bytes@Managed`.
- **Managed string/bytes operations** (`StringBuiltins`/`BytesBuiltins`): for `string`, `from`, `as_view`, `slice`, `to_managed`, `clone_with`, `append`, `length`, `is_empty`; for `bytes`, `with_capacity`, `from_slice`, `as_view`, `as_slice`, `to_managed`, `view`, `view_string`, `append`, `length`, `is_empty`.
- **Region & cancel-token modal hooks** (`BuiltinModalSymMap`): `Region::new_scoped`/`alloc`/`mark`/`reset_to`/`reset_unchecked`/`freeze`/`thaw`/`free_unchecked`, plus `CancelToken::new` and the `CancelToken::Active::*` operations.
- **Capability host-primitives** (`BuiltinSym`): the `IO::*`, `Network::restrict_to_host`, `HeapAllocator::with_quota`/`alloc_raw`/`dealloc_raw`, `Reactor::run`/`register`, `Time`/`MonotonicTime`/`WallTime::*`, and `System::*` methods. Source calls them with the `~>` operator on a `$Capability` value; they lower directly to these builtin symbols rather than to emitted vtable-call sequences.

The runtime symbol signatures are fixed; for example (§24.6.3):

```text
RuntimeSig(PanicSym)       = ⟨[⟨⊥, `code`, u32⟩], !⟩
RuntimeSig(ContextInitSym) = ⟨[], TypePath(["Context"])⟩
RuntimeSig(StringDropSym)  = ⟨[⟨`move`, `value`, string@Managed⟩], ()⟩
RuntimeSig(BytesDropSym)   = ⟨[⟨`move`, `value`, bytes@Managed⟩], ()⟩
```

Two attribute requirements the runtime library MUST satisfy (`RuntimeDeclsOk`, §24.6.3): the `panic` symbol carries `{noreturn, nounwind}`, and every other runtime symbol carries `nounwind`. Linking against a runtime library that is missing or unreadable is reported as `E-OUT-0407`; one that lacks a required runtime symbol is `E-OUT-0408`.

The heap allocator (`HeapAllocator`) has precise quota accounting semantics (§24.6.4): `with_quota(q)` produces a child heap `⟨parent, q, used=0⟩`; `alloc_raw(0)` returns null without mutation; an allocation exceeding any ancestor's headroom returns null without mutation; a successful allocation increases `used` for every ancestor by `count`; `dealloc_raw(null, _)` is a no-op; deallocation frees exactly once and decreases each ancestor's `used` by the *recorded* size (the `count` argument is non-authoritative); and deallocating a pointer not returned by `alloc_raw` (double-free or foreign pointer) is undefined.

### 28.8 Backend Requirements (Reference Level)

The backend targets **LLVM 21.1.8** (`IRTarget = "LLVM-21.1.8"`, `LLVMToolchain = "21.1.8"`); the hosted compiler is built against an in-process LLVM of that version. The following facts are observable or interop-relevant; the rest of §24.7 (SSA, branch, GEP lowering) is pure codegen and does not affect program semantics.

- **Module header** (§24.7.1): every module emits `target datalayout` and `target triple` for the selected profile (e.g. `"x86_64-pc-windows-msvc"` for `x86_64-win64`).
- **Opaque pointers** (§24.7.2): all pointers lower to `ptr` in address space 0; there are no typed LLVM pointers.
- **Pointer ABI attributes** (§24.7.3): a `Ptr<U>@Valid` parameter lowers with `nonnull`, `dereferenceable(sizeof(U))`, `align(alignof(U))`, `noundef`; a `unique` pointer parameter adds `noalias`, a `const` one adds `readonly`. `@Null`/`@Expired`/raw pointers carry no such attributes.
- **UB and poison avoidance** (§24.7.4): the emitted IR is required to be `LLVMUBSafe` — no `undef`/`poison`, no `nsw`/`nuw`, arithmetic via checked `*.with.overflow` intrinsics, checked div/rem and shifts, and bounds-checked GEP. This is why integer overflow, division by zero, bad shifts, and out-of-bounds indexing **panic** (codes `0x0004`/`0x0003`/`0x0005`/`0x0006`) rather than producing UB.
- **Memory intrinsics** (§24.7.5): aggregate copies use `llvm.memcpy` when non-overlap is known, otherwise `llvm.memmove`; aggregate zeroing uses `llvm.memset`.
- **Type mapping** (§24.7.7): primitives map as expected (`bool → i8`, `char → i32`, `usize`/`isize → i64`, `()`/`!` → empty); records/tuples become structs with explicit padding honoring the computed layout; slices become `{ptr, usize}`; `string@Managed`/`bytes@Managed` become `{ptr, usize, usize}` and the `@View` forms `{ptr, usize}`; dynamics become `{data, vtable}` pairs.
- **Call ABI mapping** (§24.7.10): `SRet` returns add a leading `sret noalias` out-pointer and lower the LLVM return to `void`; `ByRef` parameters lower to pointer parameters (a `const`-qualified pointer in the lowered signature).
- **Drop glue & vtables** (§24.7.11): each drop type gets an internal drop-glue symbol `ultraviolet::runtime::drop::…` (`DropGlueSym`), referenced from its vtable header alongside `sizeof`/`alignof` (`VTableHeader(T) = [sizeof(T), alignof(T), DropGlueSym(T)]`). This is how a `$Class` dynamic value's destructor is found at runtime.

### Idioms & Best Practices

- **Keep `main` thin and capability-scoped.** Take the narrowest context bundle `main` actually needs (a projected record whose fields are exact context-bundle field names like `io: $IO`, `time: $Time`), not the whole `Context`, and return a meaningful `i32`. The style guide's capability-narrowing rule applies at the entry point too.
- **Call capabilities with `~>`, not `.`.** A `$IO`/`$Time`/`$System` value is a dynamic class object; its methods dispatch with `base~>method(args)` (for example `ctx.io~>write_stdout(...)`). Field access into a record uses `.` (`ctx.io`); the method call on the resulting dynamic value uses `~>`.
- **Model resource ownership with `drop`, not manual cleanup.** Give a record a `drop(~!)` method to release its handle/buffer; let deterministic destruction free it on every exit path. Prefer this over scattering manual `close`/`free` calls.
- **Prefer `modal` types for lifecycle-heavy resources.** Per the style guide, when available fields or operations differ by lifecycle state (open/closed, active/cancelled), use a `modal` type rather than booleans; the modal-state payload still participates in deterministic drop.
- **Use `defer` for paired teardown that isn't a value's own destructor** — flushing a log, restoring a flag, emitting a trailer. Keep the block `()`-typed and free of `return`/`break`/`continue`.
- **Make destructors infallible.** A `drop` or `defer` that panics while already unwinding aborts the process. Do not perform fallible work in cleanup; if you must, ensure it cannot panic during an in-progress panic.
- **Favor immutable statics and `let` constants.** A `let` static with a constant literal becomes read-only data (`GlobalConst`) with no runtime init cost or ordering hazard. Reserve runtime-initialized statics and `var` for justified boundary services; never expose public mutable module-scope state.
- **Don't engineer initialization-order tricks.** Static init order is a topological sort of the dependency graph; rely on declared dependencies, not on source ordering, and avoid cross-module init cycles (they fail to compile with `Topo-Cycle`).
- **Pin foreign symbol names explicitly.** Use `#export("C")` plus `#mangle("exact_name")` for symbols foreign code must find (`#mangle` wins, fixing the literal symbol); let `extern "C"` use raw names for symbols you import. Keep ABI-facing code in thin, dedicated boundary modules, and mark `#layout(C)` on aggregates that cross the boundary.

### Pitfalls & Diagnostics

- **Wrong `main` shape.** Forgetting `public`, returning something other than `i32`, taking zero or multiple parameters, taking a non-context-bundle parameter, or making `main` generic all fail: `E-MOD-2431` (signature), `E-MOD-2432` (generic), `E-MOD-2430` (multiple), `E-MOD-2434` (missing).
- **Calling a capability with `.` instead of `~>`.** `ctx.io.write_stdout(...)` is wrong: the capability value is a dynamic class object and dispatches with `~>`. Use `ctx.io~>write_stdout(...)`. The method name is the spec's snake_case name (`write_stdout`, `monotonic`, `now`), not a camelCase rename.
- **Inventing context-bundle fields.** A projected `main` parameter record may only declare the fixed context-bundle field names (`io`, `net`, `heap`, `sys`, `reactor`, `time`, `cpu`, `gpu`, `inline`) with their exact `$`-prefixed dynamic types. `MonotonicTime`/`WallTime` are not context-bundle fields — obtain them from `$Time` via `~>monotonic()`/`~>wall()`.
- **`defer` returning a value.** A `defer` block whose final expression is non-`()` is `E-SEM-3151`. End defer blocks on a unit expression (or a statement).
- **Non-local control flow in `defer`.** `return`/`break`/`continue` escaping a `defer` block is `E-SEM-3152`.
- **`Bitcopy` + `Drop` conflict.** A type cannot be both trivially copyable and have a destructor; adding a `drop` method to a type that is otherwise `Bitcopy` is a compile-time conflict (§14.10, **BitcopyDrop-Conflict**).
- **Wrong `drop` signature.** A `drop` method that is not exactly `unique self` (`~!`), no extra params, returning `()` does not satisfy `HasDropMethod` and will not be recognized as a destructor.
- **Init cycles.** A cycle in the eager static-init dependency graph is rejected (`Topo-Cycle`). Break the cycle or make the dependency lazy.
- **Reading a poisoned static.** If a static's module (or a module it depends on) panicked during init, touching that static (or calling into that module) panics with `InitPanic` (code `0x000A`). Treat an init panic as fatal to the affected subsystem; do not try to "use it anyway."
- **Panicking destructors during unwind = abort.** A second panic raised by cleanup while a panic is already propagating terminates the process (no recovery). This is the single most dangerous destructor bug.
- **Assuming statics are torn down after a panic.** Static deinit runs only on the clean exit path. If `main` leaves a panic pending, the process diverges through the runtime `panic` symbol and `var`/managed statics are **not** dropped — do not rely on static teardown for correctness-critical flushing on the panic path.
- **Missing or mismatched runtime library.** Linking without `UltravioletRT.*` for the target (or unreadable) fails as `E-OUT-0407`; one missing a required symbol fails as `E-OUT-0408`. Backend/emit failures surface across `E-OUT-0401`…`E-OUT-0418` (object/IR emission, type mapping, call-ABI lowering, vtable/literal emission, entry/context construction, poisoning instrumentation).
- **FFI aggregate passing surprises.** An aggregate ≤ 16 bytes with alignment ≤ 8 crosses a C boundary by value; a larger one is passed by reference. If you assumed by-value for a big aggregate, your foreign signature is wrong — let `ForeignABIParam` (size/align) decide and mark FFI-crossing types `FfiSafe`.
