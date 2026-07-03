## 30. Style, Naming & Engineering Best Practices

This chapter is the engineering standard for all Ultraviolet code. Where earlier chapters define *what is legal*, this chapter defines *what is correct, idiomatic, and reviewable*. Its authoritative source is the Ultraviolet style guide; the rules below reproduce it in full and ground each one in the language constructs that enforce it. For the precise grammar and semantics behind a cited construct, see the relevant feature chapters by name: **Lexical Structure**, **Names, Visibility & Resolution**, **Module Forms**, **Data Types**, **Modal Types**, **Generics**, **Classes & Implementations**, **Refinement & Capability**, **Procedures & Methods**, **Contracts & Invariants**, **Permissions**, and **FFI & Foreign Contracts**.

The governing philosophy is fixed:

- Express correctness in the code, not in comments.
- Use the type system, `modal` types, contracts, invariants, and narrow capabilities before reaching for weaker runtime-only validation.
- Keep authority narrow. Pass only the capabilities and data that are actually used.
- Prefer safe language patterns even when they require more code.
- Treat `unsafe` and `#dynamic` as deliberate boundary tools, not convenience escapes.
- Keep APIs small, explicit, and stable.
- Optimize for legibility during review over terseness while avoiding unnecessary ceremony.

> **A note on two spellings.** The prose style guide refers informally to `[[dynamic]]` discipline, but `[[ ]]` is **not** an Ultraviolet token and will not lex. The only spelling the grammar admits is the attribute `#dynamic` (Appendix B: `dynamic_attribute ::= "#" "dynamic"`), whose delimiter is the `#` operator. The style guide also writes "constants" with a `const` keyword in prose, but Ultraviolet has **no `const` declaration form**: `const` is exclusively a *permission* (`permission ::= "const" | "unique" | "shared"`). Module-scope and named constants are immutable `let` bindings (`static_decl`). Every example in this chapter uses the spec-correct forms.

### 30.1 Naming

Ultraviolet naming is mechanical and exhaustive: every declaration category has exactly one correct casing. The compiler does not enforce casing, so consistency is a review obligation, and deviations are defects.

#### 30.1.1 General Rules

- Use descriptive names. Do not abbreviate unless the abbreviation is well-established in the problem domain.
- Preserve established acronyms and initialisms in their conventional form.
- Do not encode type information in variable names. The type system already states the type; a name like `frame_index_u32` is redundant and a defect.
- Do not use name churn to simulate shadowing or ownership changes. Alias only with `using ... as ...` where aliasing is genuinely needed. The local-alias form is `using_local_stmt ::= "using" identifier "as" identifier terminator`; the import/re-export alias form is the `("as" identifier)?` tail of `import_decl` and `using_clause`.

#### 30.1.2 The Naming Matrix

This matrix is complete and binding. Every declaration you write falls into exactly one row.

| Category                                                   | Style                          | Examples                                                                                   |
| ---------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------ |
| Assemblies                                                 | `PascalCase`                   | `Grimoire`, `Vellum`, `Generated`, `GrimDemo`                                               |
| Modules and submodules                                     | `PascalCase` per path segment  | `Grimoire::Behavior::Compiler`, `Grimoire::Frame::Loop`, `Grimoire::Inkwell::FrameGraph`   |
| Directories                                                | `PascalCase`                   | `Behavior`, `Frame`, `FrameGraph`                                                           |
| Files                                                      | `PascalCase.uv`                | `SessionConfig.uv`, `Loop.uv`, `FrameGraph.uv`                                              |
| Types (`record`, `class`, `modal`, `enum`, type aliases)   | `PascalCase`                   | `SessionContext`, `AssetManifest`, `PlaybackState`                                          |
| Procedures and methods                                     | `camelCase`                    | `bootSession`, `buildPackage`, `extractFrame`                                               |
| Transitions                                                | `camelCase`                    | `beginPlayback`, `finishImport`, `enterEditor`                                              |
| Local variables                                            | `snake_case`                   | `frame_index`, `asset_id`, `package_root`                                                   |
| Parameters                                                 | `snake_case`                   | `config_path`, `frame_delta`, `device_handle`                                               |
| Public/internal instance fields                            | `snake_case`                   | `package_id`, `world_id`                                                                    |
| Private instance fields                                    | `_snake_case`                  | `_device`, `_frame_index`, `_package_cache`                                                 |
| Constants and static values                                | `SCREAMING_SNAKE`              | `MAX_SUBTICKS`, `DEFAULT_TIMEOUT_MS`                                                        |
| Private static fields                                      | `_SCREAMING_SNAKE`             | `_FRAME_POOL_SIZE`, `_DEFAULT_LAYER_MASK`                                                   |
| Enum variants                                              | `PascalCase`                   | `Windowed`, `BorderlessFullscreen`, `Cooked`                                                |
| Boolean variables and fields                               | predicate `snake_case`         | `is_ready`, `has_focus`, `can_present`, `should_reload`                                     |
| Boolean procedures and methods                             | predicate `camelCase`          | `isReady`, `hasFocus`, `canPresent`, `shouldReload`                                         |
| Generic type parameters                                    | `PascalCase` with `T` prefix   | `TValue`, `TState`, `TResource`                                                             |

A worked declaration touching most categories. Note that module-scope constants are `let` statics with `SCREAMING_SNAKE` names and a mandatory type annotation — not a `const` declaration, which does not exist:

```ultraviolet
//! Session bootstrapping for the playback subsystem.

/// Maximum number of physics subticks per rendered frame.
public let MAX_SUBTICKS: u32 = 8

let _FRAME_POOL_SIZE: usize = 256

/// An immutable snapshot of the session that was booted.
public record SessionContext {
    public package_id: u64
    public world_id: u64
    _device: DeviceHandle
}

/// Boot a session from the supplied configuration.
public procedure bootSession<TResource>(
    config_path: string,
    frame_delta: f32
) -> SessionContext {
    let frame_index: u32 = 0
    let is_ready: bool = frame_index < MAX_SUBTICKS
    return buildContext(config_path, is_ready)
}
```

#### 30.1.3 Acronyms and Initialisms

- Preserve well-known acronyms in their established form.
- Preferred: `SDL3Bridge`, `D3D12Device`, `UUID`, `RGBA8Texture`, `CPUTime`.
- Do **not** normalize established acronyms into mixed-case words such as `Sdl3Bridge`, `D3d12Device`, `Uuid`, or `CpuTime`.

The rule is that an acronym is an atomic unit: it keeps its full casing rather than being treated as an ordinary word boundary.

#### 30.1.4 Naming Exceptions

The matrix yields only where the language or an external contract mandates a different name:

- **Language-mandated names** may break local convention. The executable entry point is named `main` (see **30.3.3**); inside a `Main.uv` file the entry procedure is still `main`, not `entryPoint` or similar. The grammar requires this exact name (`MainSigOk` requires the declared name `main`).
- **Foreign ABI names**, serialized schema keys, file-format field names, and other externally defined identifiers may preserve external casing where compatibility requires it. This is why FFI lives behind boundary modules (**30.7**): the external casing stays contained.
- **Generated code** may use narrower machine-oriented naming if required for stable, deterministic generation, but should still stay close to this guide when practical.

### 30.2 Formatting

#### 30.2.1 Layout

- Use `4` spaces for indentation.
- Target `100` columns maximum.
- Use same-line C/K&R braces.

```ultraviolet
procedure buildFrame(request: FrameRequest) -> FrameReply {
    if should_skip {
        return FrameReply::Skip
    }

    let frame_reply: FrameReply = runFrame(request)
    return frame_reply
}
```

- The style guide permits omitting control-flow braces for a single-statement body when the result is still immediately legible. **Grammar caveat:** Appendix B defines `if_tail ::= block_expr (...)` and `loop_expr ::= "loop" loop_condition? loop_invariant? block_expr`, and `block_expr ::= "{" statement* expression? "}"` always carries braces. The grammar therefore requires a braced body on `if` and `loop`; a truly braceless body does not parse. To stay portable and guaranteed-to-compile, **write braces on every control-flow body in this handbook's examples**. Treat the "omit braces" allowance as a stylistic aspiration the grammar does not yet realize.
- Use braces when the body is multiline, nested, or likely to grow.
- Do not use alignment-based formatting that depends on manual column spacing. Aligning `:` or `=` across lines with runs of spaces is forbidden — it produces noisy diffs and breaks the moment a name changes.

#### 30.2.2 Line Breaking

- Use newlines as the default statement terminator. The grammar makes the newline a first-class terminator: `terminator ::= ";" | newline`.
- Use `;` only when multiple small statements on one line are clearly justified or surrounding syntax requires it.
- When a signature, argument list, type parameter list, or initializer exceeds the line limit, wrap to one item per line.

```ultraviolet
procedure buildSession(
    session_context: SessionContext,
    package_registry: PackageRegistry,
    graph_registry: GraphRegistry,
    frame_config: FrameConfig
) -> Session {
    return assembleSession(session_context, package_registry, graph_registry, frame_config)
}
```

```ultraviolet
let session: Session = buildSession(
    session_context,
    package_registry,
    graph_registry,
    frame_config,
)
```

Appendix B admits a trailing comma on both `param_list ::= param ("," param)* ","?` and `argument_list ::= argument ("," argument)* ","?`, so one-item-per-line wrapping with a trailing comma is legal and produces minimal diffs when items are added. The braced `using_list ::= "{" using_specifier ("," using_specifier)* ","? "}"` admits a trailing comma as well.

#### 30.2.3 Spacing and Blank Lines

- Put one blank line between top-level declaration groups.
- Use blank lines to separate logical phases inside longer procedures.
- Avoid vertical whitespace that does not communicate structure.
- Keep related declarations visually grouped.

The standard is communicative whitespace: a blank line marks a phase boundary, not decoration.

### 30.3 Module, Directory, and File Organization

In Ultraviolet, the module graph is the *directory* graph. A directory containing at least one `.uv` file is a module, and all `.uv` files in that directory form one compilation unit. This single fact drives the entire organization standard. See **Module Forms** and **Names, Visibility & Resolution** for the resolution semantics.

#### 30.3.1 Module Structure

- Directories define modules. Every intended public or internal submodule must have its own directory.
- Do not treat file names as the module boundary. Multiple `.uv` files in the same directory belong to the same module — splitting a file does not split the module.
- Keep public API roots stable. Reorganize internals freely, but do not rename public module roots casually. Renaming a public root is a breaking change to every importer (`import_decl ::= attribute_list? visibility? "import" module_path ("as" identifier)?`).

```text
Grimoire/                  (assembly: Grimoire)
  Frame/                   (module: Grimoire::Frame)
    Loop.uv
    FrameReply.uv          (same module as Loop.uv — directory is the boundary)
    FrameGraph/            (submodule: Grimoire::Frame::FrameGraph)
      FrameGraph.uv
  Behavior/                (module: Grimoire::Behavior)
    Compiler/              (submodule: Grimoire::Behavior::Compiler)
      Compiler.uv
```

#### 30.3.2 File and Module Size

- Keep files around `~400` lines or less.
- Split earlier when a file mixes multiple responsibilities, mixes large public API surfaces with implementation detail, or becomes difficult to review.
- Prefer splitting by responsibility, lifecycle phase, or subsystem boundary rather than by arbitrary size alone.
- If a directory accumulates unrelated concepts, introduce submodules (new subdirectories) instead of continuing to grow a flat module.

Because files in one directory share a module, splitting a 600-line `Loop.uv` into `Loop.uv` + `LoopScheduling.uv` is a pure readability win with no API change.

#### 30.3.3 Special Files

- Use `Main.uv` for executable-root source files when the file name is project-controlled, but the entry procedure inside remains `main`.
- Use `Api.uv` only for thin facade or root export surfaces.
- Keep facade files small. They should coordinate exports, not accumulate deep logic.

A facade `Api.uv` re-exporting a module's public surface uses `public using` (re-exporting a non-public item is rejected by `E-MOD-1205`):

```ultraviolet
//! Public facade for the Frame module. Coordinates exports only.

public using Grimoire::Frame::Loop::{ FrameLoop, FrameRequest }
public using Grimoire::Frame::FrameGraph::FrameGraph
```

A `Main.uv` whose entry procedure is `main`. The entry signature is fixed by the language: `main` must be `public`, take exactly one context-bundle parameter (raw `Context` is a valid context bundle), and return `i32` (`MainSigOk`; an invalid signature is `E-MOD-2431`):

```ultraviolet
//! Executable root.

import Grimoire::Frame

/// Program entry point. The name `main`, the single `Context` parameter, and
/// the `i32` return are all language-mandated (see 30.1.4 and `MainSigOk`).
public procedure main(context: Context) -> i32 {
    let session: Session = bootSession(context)
    runFrameLoop(session)
    return 0
}
```

### 30.4 Imports and Visibility

#### 30.4.1 Import Ordering

- Order imports from most foundational to most specific.
- Put foundational and built-in imports first.
- Put engine and project imports next.
- Put aliases last.
- If an implementation module uses `using module::*`, keep it after regular imports and regular `using` declarations.

#### 30.4.2 `using` Rules

The `using` declaration is `using_decl ::= attribute_list? visibility? "using" using_clause`, and `using_clause` has three shapes: a single specifier with optional alias (`module_path "::" identifier ("as" identifier)?`), a braced list (`module_path "::" using_list`), and the wildcard (`module_path "::" "*"`).

- `using module::*` is allowed only in internal or implementation modules.
- Never use wildcard `using` in public API modules — it makes the imported surface invisible at the use site.
- Prefer importing exact names or explicit aliases in public-facing code.
- Use `using ... as ...` only when the alias meaningfully improves clarity or avoids a real collision.

```ultraviolet
// Foundational / built-in first.
import Ultraviolet::Collections

// Engine and project next.
import Grimoire::Frame
import Grimoire::Behavior::Compiler

// Explicit names in public-facing code.
using Grimoire::Frame::Loop::{ FrameLoop, FrameRequest }

// Aliases last, and only to resolve a real collision.
using Grimoire::Behavior::Compiler::Diagnostic as BehaviorDiagnostic
```

#### 30.4.3 Visibility

Visibility is `visibility ::= "public" | "internal" | "private"`, with the access rules: `public` is reachable everywhere; `internal` is reachable within the same assembly (the internal-access rule requires the same assembly); `private` is reachable only within the declaring module (the private-access rule requires the access to occur in the declaring module).

- Always write visibility explicitly where the language allows it.
- Do not rely on omitted visibility defaults for project code.
- Treat visibility as part of the API contract, not as an optional decoration.
- A field's visibility may not exceed its containing type's visibility (`FieldVisOk`): a `public` field on an `internal` record is ill-formed. Keep type visibility at least as broad as its most visible member.

```ultraviolet
/// Public: part of the stable API contract.
public record AssetManifest {
    public asset_id: u64
    // Private: implementation detail, never escapes the module.
    _checksum: u64
}

/// Internal: shared across this assembly, not exported to consumers.
internal procedure validateManifest(manifest: AssetManifest) -> bool {
    return manifest.asset_id != 0
}
```

### 30.5 Type Design

The central decision is `record` vs `class` vs `modal`. Choose by the *nature of the data*, not by convenience.

#### 30.5.1 `record`, `class`, and `modal`

- Use `record` for plain value data, descriptors, configuration, snapshots, and other data-first structures. A record body holds only fields and methods (`record_member ::= record_field_decl | method_def`); there are no associated constants or static members inside a type — keep named constants as module-scope `let` statics beside the type.
- Use `class` only when shared identity, polymorphism, or reference-oriented behavior is actually required.
- Use `modal` for state-based code. If behavior, available fields, or allowed operations differ by lifecycle state, model that with `modal` types rather than booleans, comments, or informal conventions (`modal_decl ::= ... "modal" identifier ... "{" state_block+ "}" type_invariant?`).
- Modal types and contracts are the preferred way to model protocols, resource states, runtime sessions, imports, cooking phases, and other lifecycle-heavy flows. See **Modal Types**.

A `record` for a value snapshot; a `modal` for a lifecycle. Each `state_block ::= "@" state_name "{" state_member* "}"` groups the fields, methods, and transitions valid in that state. Transitions take no receiver shorthand (they move `self` implicitly) and **must** carry a body (`transition_def ::= ... "transition" identifier "(" param_list ")" "->" "@" target_state block_expr`). State methods declare a receiver shorthand (`~`, `~!`, or `~%`):

```ultraviolet
/// Value data — a configuration descriptor. `record` is correct.
public record FrameConfig {
    public target_fps: u32
    public vsync_enabled: bool
}

/// Lifecycle-driven — fields and operations differ per state. `modal` is
/// correct: the import handle cannot be read before it is opened.
public modal ImportSession {
    @Closed {
        public source_path: string

        /// Open the session. Transitions move `self` and yield the @Open state.
        public transition open(handle: FileHandle) -> @Open {
            return ImportSession@Open { _handle: handle }
        }
    }
    @Open {
        _handle: FileHandle

        /// Read the next chunk. `~` borrows the receiver immutably.
        public procedure readChunk(~) -> bytes {
            return readFrom(self._handle)
        }

        /// Finish and return to the closed state.
        public transition finish() -> @Closed {
            return ImportSession@Closed { source_path: pathOf(self._handle) }
        }
    }
}
```

#### 30.5.2 Member Ordering

Inside a type, order members from highest-level and most stable to most local:

1. associated named constants (kept as adjacent module-scope `let` statics, since types hold no const members),
2. fields,
3. invariants/contracts,
4. factories/lifecycle,
5. public API,
6. then private helpers.

In `modal` types, order states in lifecycle order (`state_block+` in declaration order). Within a state, keep transitions and state-specific public behavior near the state fields they govern.

A type with an invariant: because **types with type invariants must not declare public mutable fields** (`E-SEM-2824`), the mutable state is private and mutation flows through a method that takes a mutating receiver (`~!`). The invariant is written after the record body:

```ultraviolet
public record PlaybackState {
    // 2. fields — private, because the invariant constrains them.
    public target_fps: u32
    _speed: f32
    _frame_cursor: u64

    // 5. public API
    public procedure advance(~!, delta: u64) -> () {
        self._frame_cursor += delta
    }

    public procedure speed(~) -> f32 {
        return self._speed
    }

    // 6. private helpers
    procedure clampSpeed(~) -> f32 {
        return self._speed
    }
} |: { self._speed > 0.0 }
```

### 30.6 Contracts, Invariants, and Safety Semantics

#### 30.6.1 Contracts Are Mandatory Where Expressible

If a rule about safety, range, state, ownership, lifetime, authority, or valid sequencing can be expressed with contracts or invariants, express it in code. This is not optional.

- Do not leave machine-checkable rules as comments alone.
- Prefer precise contracts over broad defensive code where the language can state the constraint directly.
- Public APIs, cross-module APIs, lifecycle transitions, and FFI wrappers should be especially strict about contracts.

A contract clause is attached after the signature (`procedure_decl ::= attribute_list? visibility? "procedure" identifier generic_params? signature contract_clause? block_expr`) with the form:

```ebnf
contract_clause    ::= "|:" contract_body
contract_body      ::= precondition_expr "|=" postcondition_expr
                     | "|=" postcondition_expr
                     | precondition_expr
precondition_expr  ::= predicate_expr
postcondition_expr ::= predicate_expr
```

Postconditions may reference the result via the intrinsic `@result` and the pre-call value of an expression via `@entry(expr)`. In grammar these are decorated identifiers (`contract_intrinsic ::= decorated_identifier("@", "result") | decorated_identifier("@", "entry") "(" expression ")"`), not combined lexer tokens. Contract predicates must be pure (`WF-Contract` requires `pure(P_pre)` and `pure(P_post)`); a predicate that calls an effectful or impure procedure is ill-formed.

```ultraviolet
/// Divide `numerator` by `denominator`.
///
/// Precondition: the denominator is nonzero.
/// Postcondition: the result, multiplied back, does not exceed the numerator.
public procedure divFloor(numerator: u64, denominator: u64) -> u64
    |: denominator != 0 |= @result * denominator <= numerator
{
    return numerator / denominator
}
```

A type invariant is written after the type body with the form `type_invariant ::= "|:" "{" predicate_expr "}"`. It is enforced at three points: post-construction, before any public receiver-taking call, and before any mutating receiver-taking call returns. **Types with type invariants must not declare public mutable fields** (`E-SEM-2824`) — that constraint pushes the prefer-invariants-over-validation rule directly into the type system, forcing mutation through methods:

```ultraviolet
/// A texture dimension that is always strictly positive in both axes.
public record TextureExtent {
    public width: u32
    public height: u32
} |: { self.width > 0 && self.height > 0 }
```

A loop invariant uses the same `"|:" "{" predicate_expr "}"` form between the loop condition and the body (`loop_expr ::= "loop" loop_condition? loop_invariant? block_expr`). It is checked before the first iteration, at the start of every subsequent iteration, and immediately after termination:

```ultraviolet
procedure sumTo(limit: u32) -> u32 {
    var total: u32 = 0
    var i: u32 = 0
    loop i < limit |: { i <= limit } {
        total += i
        i += 1
    }
    return total
}
```

#### 30.6.2 Capability Passing

Authority in Ultraviolet is carried by capability values, with `Context` as the sole explicit root carrier (`CapInType(TypePath([Context])) = {IO, Network, HeapAllocator, Reactor, ExecutionDomain, System, Time}`). There is no ambient authority: a procedure can only cause an effect if it received a capability that grants it, and the compiler rejects any direct call whose target requires capabilities the caller does not hold (`CapReq(d_tgt) ⊆ EffectiveCapReq(d_src)`). See **Refinement & Capability** and **Permissions**.

The style rules follow from this model:

- Do not pass large context bundles through ordinary code. Threading whole `Context` everywhere grants every callee the full authority set.
- Pass only the exact capabilities a procedure or method uses. A dynamic capability object type is written `$ClassPath` (`dynamic_type ::= "$" class_path`); a parameter of type `$IO` confers only IO authority.
- If several capabilities repeatedly travel together at a real subsystem boundary, define a narrow projected context bundle for that boundary. A context bundle is a `record` whose fields are drawn from the bundle field set (`io: $IO`, `net: $Network`, `heap: $HeapAllocator`, `sys: $System`, `reactor: $Reactor`, `time: $Time`, `cpu`/`gpu`/`inline: $ExecutionDomain`) or are themselves context bundles.
- Do not thread through broad "god context" objects for convenience.
- Capability narrowing is part of API design, not an optional cleanup pass.

Method calls on a capability value use the `~>` postfix form (`postfix_suffix` includes `"~>" identifier "(" argument_list? ")"`); `.` is for field and tuple access only:

```ultraviolet
// Wrong: hands the whole authority root to a procedure that only writes a log.
procedure logFrameWide(context: Context, message: string) -> () {
    // grants IO, Network, HeapAllocator, Reactor, ExecutionDomain, System, Time
}

/// Append `message` to the frame log.
///
/// Capability: requires only `$IO`; confers no network, time, or allocation
/// authority.
public procedure logFrame(io: $IO, message: string) -> () {
    io~>writeLine(message)
}
```

#### 30.6.3 State and Validation

- Prefer state encoded in types over state encoded in booleans (reach for `modal`, not an `is_open` flag).
- Prefer contracts over ad hoc runtime checks when the language can express the rule.
- Prefer invariants over duplicated validation logic — one invariant on the type replaces a validation call at every mutation site.
- Prefer compile-time safety and structural constraints over convention-based usage.

### 30.7 `unsafe`, `#dynamic`, and FFI

These are boundary tools. Each one suspends a safety guarantee, so each one must be small, local, justified, and re-wrapped in a safe API.

#### 30.7.1 `unsafe`

`unsafe` is a block statement (`unsafe_block ::= "unsafe" block_expr`).

- `unsafe` is permitted only when safe language patterns genuinely cannot replicate the required behavior.
- More code or more effort is not a justification for `unsafe`.
- Keep `unsafe` blocks as small and local as possible.
- Wrap unsafe operations in safe APIs that re-establish project invariants.
- Every unsafe boundary must document ownership, lifetime, thread affinity, and caller obligations.

The caller obligation is encoded as a precondition contract; the bounds fact it establishes is what makes the inner raw load defensible:

```ultraviolet
/// Read the element at `index` without bounds checking.
///
/// Ownership: `buffer` is borrowed for the call only.
/// Lifetime: the returned value is copied; no reference escapes.
/// Thread affinity: caller must hold exclusive access to `buffer`.
/// Caller obligation (enforced by contract): `index < count`.
public procedure elementUnchecked(buffer: [u8], count: usize, index: usize) -> u8
    |: index < count
{
    unsafe {
        return rawLoad(buffer, index)
    }
}
```

#### 30.7.2 `#dynamic`

`#dynamic` (`dynamic_attribute ::= "#" "dynamic"`) marks a declaration or expression as requiring runtime verification where static verification is insufficient. Its scope is lexical and does not propagate through procedure calls.

- Use `#dynamic` only when the intended semantics are truly dynamic.
- Do not use `#dynamic` to bypass correct static conformance.
- Do not use `#dynamic` to compensate for poor API design, weak type modeling, or missing contracts.
- If a static formulation is possible and matches the intended behavior, use it.

The compiler defends this discipline: `#dynamic` on a contract clause is `E-CON-0410`, on a type alias is `E-CON-0411`, and on a field declaration is `E-CON-0412`; if every proof in a `#dynamic` scope succeeds statically, the implementation warns (`W-CON-0401`) — a signal that the attribute is unnecessary and should be removed.

```ultraviolet
/// Verify the key acquisition at runtime, because the access pattern is
/// genuinely data-dependent and cannot be proven statically.
#dynamic
public procedure acquireDynamicKey(table: KeyTable, slot: u32) -> KeyHandle {
    return table~>acquire(slot)
}
```

#### 30.7.3 FFI Boundaries

Foreign interaction is fenced into dedicated boundary modules. The language enforces part of this: foreign code MUST NOT receive or return capability-bearing values — any extern or hosted-export signature containing `Context`, a capability class, or a dynamic class object is ill-formed, and a region-local raw pointer crossing an FFI boundary is `E-SYS-3360`.

- Isolate foreign interaction to dedicated boundary modules (their own directory).
- Keep ABI-facing code thin and explicit. Extern blocks carry the ABI string (`extern_block ::= attribute_list? visibility? "extern" abi_string? "{" extern_item* "}"`, with `abi_string ::= string_literal`).
- Do not let FFI concerns leak into ordinary gameplay, tooling, or simulation code.
- Prefer safe wrappers that expose project-level types and contracts instead of raw foreign handles or pointers.

Foreign procedures may carry foreign contracts (`foreign_contract ::= "|:" decorated_identifier("@", "foreign_assumes") "(" predicate_expr ")" | "|:" decorated_identifier("@", "foreign_ensures") "(" ensures_predicate ")"`, where `ensures_predicate` additionally admits `decorated_identifier("@", "error") ":" predicate_expr` and `decorated_identifier("@", "null_result") ":" predicate_expr`). These document and check the trust boundary. Raw pointer types are spelled `*imm T` / `*mut T` (`raw_pointer_type ::= "*" raw_pointer_qual type`, `raw_pointer_qual ::= "imm" | "mut"`). See **FFI & Foreign Contracts**.

```ultraviolet
//! Boundary module: the only place that touches the C image codec.

extern "C" {
    /// Raw foreign decode. ABI-facing, thin, never called outside this module.
    /// `@foreign_assumes` is the caller obligation; `@error: ...` classifies
    /// a nonzero return as failure.
    procedure uv_decode_png(data: *imm u8, len: usize, out: *mut u8) -> i32
        |: @foreign_assumes(len > 0)
        |: @foreign_ensures(@error: @result != 0)
}

/// Safe wrapper: exposes project types and a contract; no raw pointer escapes.
public procedure decodePng(data: bytes) -> RgbaImage
    |: data~>length() > 0
{
    unsafe {
        return decodeInto(data)
    }
}
```

### 30.8 Procedures and API Design

#### 30.8.1 Procedure Style

- Use `camelCase` for procedures, methods, and transitions.
- Write explicit `return` statements in non-`()` procedures. This is not merely style: the body of a procedure with a non-unit return type must end in a `return` statement (`ExplicitReturn`; the violation is `E-TYP-1507`). A trailing bare expression does not satisfy it.
- Procedures with a non-unit return type must also carry an explicit return-type annotation (`E-TYP-1508`).
- Keep procedures focused on one operation or one cohesive phase.
- Prefer small helper procedures over large deeply nested bodies.

#### 30.8.2 API Surface

- Prefer narrow, specific APIs over broad convenience APIs.
- Avoid parameter lists that mix unrelated concerns.
- Avoid wrappers or indirection that add no clarity, safety, or ownership boundary.
- Prefer a small number of strong, composable types over many weak convenience helpers.

```ultraviolet
/// Narrow and specific: one operation, cohesive parameters, explicit return.
public procedure extractFrame(graph: FrameGraph, frame_index: u32) -> Frame
    |: frame_index < graph.frame_count |= @result.index == frame_index
{
    let frame: Frame = graph~>frameAt(frame_index)
    return frame
}
```

### 30.9 Module-Scope State

Module-scope (static) declarations use the `static_decl` form: `static_decl ::= attribute_list? visibility? ("let" | "var") binding_decl`, where `binding_decl ::= pattern (":" type)? binding_op expression`. A module-scope binding requires an explicit type annotation (`E-TYP-1505`). `let` statics that are constant-initialized lower to immutable globals; `var` statics lower to mutable globals.

- Prefer immutable module-scope declarations (`let`). There is no separate `const` item — an immutable named constant *is* a `let` static.
- Avoid mutable module-scope state (`var`) except for carefully justified runtime services or boundary objects.
- Name module-scope and static values with `SCREAMING_SNAKE`.
- Name private module-scope or private static values with `_SCREAMING_SNAKE`.
- **Public mutable module-scope state is forbidden.** This is enforced by the language: `StaticVisOk(vis, mut) ⇔ ¬ (vis = public ∧ mut = var)`, so a `public var` static is rejected (`StaticVisOk-Err`). It would be shared mutable global authority that escapes the capability model.

```ultraviolet
/// Public immutable static — fine.
public let MAX_SUBTICKS: u32 = 8

// Private immutable static — fine.
let _FRAME_POOL_SIZE: usize = 256

// Justified internal mutable runtime service — internal, never public.
internal var _FRAME_POOL_LIMIT: usize = 256

// FORBIDDEN: public mutable module-scope state. Rejected by StaticVisOk.
// public var GLOBAL_FRAME: u64 = 0
```

### 30.10 Comments and Documentation

#### 30.10.1 Comments

- Use comments to explain *why*, constraints, ownership, or non-obvious intent.
- Do not narrate code that is already clear from the implementation.
- Keep comments factual and durable.
- Delete comments that become stale. A stale comment is worse than none.

#### 30.10.2 Documentation Comments

Ultraviolet distinguishes two doc-comment kinds at the lexer (`DocMarker`): `///` is item documentation (`LineDoc`), and `//!` is module documentation (`ModuleDoc`). A `///` comment attaches to the next item; a `//!` comment documents the enclosing module.

- All public modules must have `//!` module documentation.
- All public types, procedures, methods, transitions, and exported constants must have `///` documentation.
- Public documentation must cover: purpose, important preconditions, important postconditions, ownership or capability expectations, and notable failure modes.

```ultraviolet
//! Frame scheduling for the playback loop. Owns the frame pool lifecycle and
//! the per-frame capability narrowing for IO logging.

/// Schedule the next frame for rendering.
///
/// Purpose: advances the frame graph cursor and enqueues `frame_index`.
/// Precondition: `frame_index` is within the graph (enforced by contract).
/// Capability: requires only `$IO` for trace logging; no network or time.
/// Failure: returns a dropped handle if the frame pool is saturated.
public procedure scheduleFrame(io: $IO, graph: FrameGraph, frame_index: u32) -> FrameHandle
    |: frame_index < graph.frame_count
{
    io~>writeLine("scheduling frame")
    return graph~>enqueue(frame_index)
}
```

### 30.11 Review Expectations

Code review verifies that the standard above is realized *in the code*, not in the reviewer's head.

- Code should be understandable without relying on hidden context.
- Reviewers should be able to see authority boundaries, state transitions, and safety constraints directly in the code — capability parameters (`$IO`, projected context bundles), `modal` states, and contract/invariant clauses make these visible.
- Prefer code that is easy to verify over code that is merely short.
- If a design relies on a rule that the language can express, the rule belongs in the code. A reviewer who finds a machine-checkable rule living only in a comment should reject the change and ask for a contract, invariant, `modal` state, or visibility constraint instead.

### Idioms & Best Practices

- **Let the type carry the state.** Replace `is_open: bool` plus comments with a `modal` whose states (`@Closed`, `@Open`) gate fields and operations. The compiler then makes "read before open" unrepresentable rather than merely discouraged.
- **Narrow at the call, not in the body.** Accept `$IO` (a `dynamic_type`) instead of `Context` whenever a procedure only logs. The signature becomes self-documenting and the capability checker (`CapReq(d_tgt) ⊆ EffectiveCapReq(d_src)`) enforces it for free.
- **Push validation into invariants.** A `record ... } |: { ... }` validates once at every enforcement point; you delete N scattered runtime checks. Remember the corollary: such a type may not expose `public` mutable fields (`E-SEM-2824`), so model mutation through methods that take a `~!` receiver.
- **Use `@result` and `@entry(...)` to make postconditions exact.** `|= @result >= @entry(self.count)` states a real guarantee a reviewer can trust without reading the body.
- **Constants are `let` statics.** There is no `const` declaration. Put `public let MAX_X: T = ...` at module scope, with a mandatory type annotation, beside the type it serves.
- **One responsibility per file, one module per directory.** When a file crosses ~400 lines or mixes API with implementation, split it within the same directory — no API change, pure readability.
- **Facades export, they do not compute.** Keep `Api.uv` a list of `public using` re-exports; keep `Main.uv` down to wiring plus the `main` entry (`public`, one `Context` bundle parameter, `i32` return).
- **Explicit visibility on every declaration**, kept consistent with member visibility (`FieldVisOk`). Treat visibility as the API contract.
- **Trailing commas on wrapped lists.** `param_list`, `argument_list`, and `using_list` all allow a trailing comma; use it so adding an item is a one-line diff.
- **Method calls use `~>`, field access uses `.`.** `io~>writeLine(m)` is a call; `manifest.asset_id` is a field. Mixing them up does not parse.
- **Keep `unsafe` a sentence, not a paragraph.** Wrap it in a safe procedure with a contract that re-establishes the invariant the unsafe code assumed.

### Pitfalls & Diagnostics

- **Writing `[[dynamic]]`.** Not a token; `[[`/`]]` will fail to lex. The only legal spelling is the `#dynamic` attribute.
- **Writing a `const` declaration.** There is no `const` item or `const` member. `const` is a permission keyword only. Use a `let` static for a named constant; a record body holds only `record_field_decl | method_def`.
- **Using `.` for enum variants or method calls.** Enum literals are `Type::Variant` (`enum_literal ::= type_path "::" identifier variant_args?`), and method calls are `recv~>method(args)`. A dot is field/tuple access. So `FrameReply::Skip` and `io~>writeLine(...)` are correct; `FrameReply.Skip` and `io.writeLine(...)` are not.
- **Writing `-> unit`.** The unit type is spelled `()` (`unit_type ::= "(" ")"`); there is no `unit` keyword.
- **A `transition` without a body, or with a receiver shorthand.** `transition_def` requires a `block_expr` body and takes `"(" param_list ")"` — no `~`/`~!`/`~%`. The transition moves `self` implicitly and must construct and return the target state value.
- **Braceless `if`/`loop` body.** `if_tail` and `loop_expr` require a `block_expr`, which always has braces; a single-statement braceless body does not parse. Always brace control-flow bodies.
- **Non-unit procedure without an explicit trailing `return`.** Required by `ExplicitReturn`; the violation is `E-TYP-1507`. A non-unit procedure also needs an explicit return-type annotation (`E-TYP-1508`).
- **Missing type annotation on a module-scope static.** `static_decl` bindings at module scope require an annotation (`E-TYP-1505`).
- **`#dynamic` on the wrong target.** On a contract clause → `E-CON-0410`; on a type alias → `E-CON-0411`; on a field declaration → `E-CON-0412`. Apply it to declarations or expressions only.
- **`#dynamic` that proves out statically.** If every check in the scope succeeds at compile time, you get `W-CON-0401`; remove the attribute — it was bypassing a static formulation that already works.
- **Impure contract predicate.** A precondition or postcondition that calls a non-pure procedure violates `WF-Contract` (`pure(P_pre)`, `pure(P_post)`). Keep predicates to field/index access and pure operators.
- **Public mutable field on an invariant-bearing type.** Forbidden (`E-SEM-2824`). Make the field private and mutate through a method so the invariant's enforcement points fire.
- **Field more visible than its type.** `FieldVisOk` requires every field's visibility rank to be no greater than the record's. A `public` field on an `internal` record is ill-formed.
- **Public mutable module-scope state.** `public var` is rejected by `StaticVisOk` (`StaticVisOk-Err`). Keep mutable statics `internal` or `private`, or model the state behind a capability.
- **Capability under-grant at a call site.** Calling a procedure whose `CapReq` is not a subset of the caller's `EffectiveCapReq` is rejected — the fix is to thread the *specific* capability (e.g. add `io: $IO`), never to widen everything to `Context`.
- **Capability-bearing value across FFI.** An extern or hosted-export signature containing `Context`, a capability class, or a `$Class` object is ill-formed; a region-local raw pointer crossing the boundary is `E-SYS-3360`. Convert to plain data before the call.
- **`public using` of a non-public item.** A facade may only re-export public items; otherwise `E-MOD-1205`.
- **Wildcard `using ::*` in a public module.** Forbidden by the style guide; it hides the imported surface. Restrict wildcards to internal/implementation modules and list names explicitly in public API code.
- **Reserved-name collisions.** `Drop`, `Bitcopy`, `Clone`, `FfiSafe`, and `GpuSafe` are reserved foundational class names, and the integer/float/`Self`/`Context`/capability identifiers are universe-protected — do not shadow them with user declarations.
- **Acronym normalization.** `Sdl3Bridge`, `Uuid`, `CpuTime` are defects; the correct forms are `SDL3Bridge`, `UUID`, `CPUTime`.
- **Type info baked into names.** `count_u32`, `frame_ptr`, `manifest_obj` duplicate what the type already states; name for meaning, not representation.
