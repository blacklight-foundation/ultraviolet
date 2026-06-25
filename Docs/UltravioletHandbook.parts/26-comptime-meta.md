## 26. Compile-Time Execution & Metaprogramming

Ultraviolet evaluates metaprogramming in a dedicated compilation phase. **Phase 2 executes compile-time forms over the Phase 1 module set before name resolution and type checking of the expanded program.** Everything in this chapter — `comptime` blocks, compile-time procedures, reflection over `Type`, quotation and splicing, emission, and derive targets — runs entirely in Phase 2. By the time Phase 3 typing and Phase 4 lowering begin, the program has been fully *expanded*: compile-time-only items have been discarded, compile-time expressions have been replaced by literals or substituted AST, and every emitted declaration has been inserted into the module.

This phase ordering is the load-bearing idea of the whole chapter. The compiler runs `ComptimePass` over modules in a fixed order (`Phase2ModuleOrder(P)`), executes each module's compile-time sites in source order, and threads a single machine state `CtMachine = ⟨files, project_root, diags, pending_emits, next_hygiene⟩` through the whole pass. Compile-time values range over a restricted universe `CtValue` (primitives, strings, bytes, `Type`, `Ast`, tuples, arrays, slices, records, and enums) — *not* the full runtime value space.

For surrounding language constructs, see the chapters on Expressions, Statements, Type Declarations (`record`/`enum`/`modal`), Classes, and Contracts. This chapter assumes those forms and only describes how they behave under compile-time evaluation.

The surface call operator throughout this chapter is the method-call operator `~>` (for example `emitter~>emit(...)`, `introspect~>fields(...)`). The specification's prose sometimes writes these calls in dot form (`emitter.emit`, `files.read`) as shorthand for the same interface; the only spelling that parses is `~>`.

---

### 26.1 Compile-Time Forms (§22.1)

Compile-time forms are the entry points into Phase 2. There are five AST node kinds, collectively `CtNode = {CtStmt, CtExpr, CtIf, CtLoopIter, CtProc}`.

#### 26.1.1 Exact Syntax

The canonical grammar (§22.1.1), reproduced verbatim:

```ebnf
comptime_stmt           ::= attribute_list? "comptime" block_expr
comptime_expr           ::= attribute_list? "comptime" "{" expression "}"
comptime_if             ::= "comptime" "if" expression block_expr ("else" (comptime_if | block_expr))?
comptime_loop           ::= "comptime" "loop" pattern (":" type)? "in" expression block_expr
comptime_procedure_decl ::= attribute_list? "comptime" visibility? "procedure" identifier generic_params? signature contract_clause? block_expr
type_literal            ::= "Type" "::" "<" type ">"
```

Note the precise distinctions:

- `comptime` followed by a **block** (`comptime { … statements … }`) parses as a **statement** (`CtStmt`) — a compile-time *statement block* that contributes no runtime statement.
- `comptime` followed by `{ expression }` in expression position parses as a **compile-time expression** (`CtExpr`). The brace wraps a single `expression`, not a statement sequence.
- `comptime if` and `comptime loop` are *expression* forms (they appear in `ParsePrimary`/`ParseExpr`), but only the selected branch / unrolled body survives into the program.
- `comptime procedure` is a top-level item (`comptime_procedure_decl` is produced by `ParseItem`). The `comptime` keyword precedes the optional `visibility`.
- `Type::<T>` is a reflection literal; note the exact token sequence `Type` `::` `<` `type` `>`. It is detailed in §26.3.

The `comptime if` / `else comptime if` chain is exactly the grammar above: each `else` may continue with another `comptime if` or terminate in a plain `block_expr`.

#### 26.1.2 What May Run at Compile Time

Inside a compile-time body you write *ordinary* expressions, statements, and blocks. They evaluate via `CtEval`/`CtExec`, which **use the same child order, scope creation, pattern binding, control propagation, and operator semantics as the corresponding ordinary relations of Chapters 18 through 21**, but with values in `CtValue` and fixed compile-time bindings dispatched through `CtBuiltinCall`. Ordinary control flow — `if`, `if … is`, conditional and iterator `loop`, `return`, local `let`/`var`, arithmetic, indexing — is therefore available unchanged. The difference is which *types* and *operations* are admissible.

**Admissible types.** A type `T` is compile-time available iff `CtAvail(T)` holds:

```text
CtAvail(TypePrim(_))
CtAvail(TypeString(@View))     CtAvail(TypeString(@Managed))
CtAvail(TypeBytes(@View))      CtAvail(TypeBytes(@Managed))
CtAvail(TypePath([Type]))
CtAvail(TypePath([Ast]))       CtAvail(TypePath([Ast, Expr]))
CtAvail(TypePath([Ast, Stmt])) CtAvail(TypePath([Ast, Item]))
CtAvail(TypePath([Ast, Type])) CtAvail(TypePath([Ast, Pattern]))
CtAvail(TypeTuple(Ts))   ⇔ ∀ T ∈ Ts. CtAvail(T)
CtAvail(TypeArray(T, _)) ⇔ CtAvail(T)
CtAvail(TypeSlice(T))    ⇔ CtAvail(T)
CtAvail(TypePath(p))     ⇔ RecordDecl(p)=R ∧ ∀ f ∈ Fields(R). CtAvail(StripPerm(f.type))
CtAvail(TypePath(p))     ⇔ EnumDecl(p)=E ∧ ∀ v ∈ Variants(E). ∀ T ∈ PayloadTypes(v). CtAvail(StripPerm(T))
CtAvail(TypePerm(_, T))  ⇔ CtAvail(T)
```

**Forbidden types.** A type `T` is forbidden in compile-time contexts iff `CtForbiddenType(T)` holds:

```text
CtForbiddenType(T) ⇔ CapInType(T) ≠ ∅
                   ∨ StripPerm(T) = TypeModalState(_, _)
                   ∨ StripPerm(T) = TypeDynamic(_)
                   ∨ StripPerm(T) = TypePtr(_, _)
                   ∨ StripPerm(T) = TypeRawPtr(_, _)
                   ∨ StripPerm(T) = TypeFunc(_, _)
                   ∨ AliasNorm(T) = TypePath(["Context"])
```

A conforming implementation **MUST reject** any compile-time expression, compile-time variable, compile-time procedure parameter, or compile-time procedure return type for which `CtForbiddenType(T)` holds or `CtAvail(T)` does not hold (diagnostics `E-CTE-0010`, `E-CTE-0011`, `E-CTE-0012`, `E-CTE-0021`, `E-CTE-0030`, `E-CTE-0031`).

**Prohibited constructs.** The following are prohibited *inside* compile-time execution (diagnostic `E-CTE-0020`):

- region allocation and frame operations;
- key acquisition blocks and dynamic key synchronization;
- `parallel`, `spawn`, `dispatch`, `wait`, `yield`, `yield from`, `sync`, `race`, and `all`;
- raw-pointer dereference, `transmute`, and any `unsafe`-only operation;
- any call that crosses an FFI boundary.

If compile-time evaluation fails to terminate it is `E-CTE-0022`; if it panics it is `E-CTE-0023`.

#### 26.1.3 `comptime` Statement and `comptime` Expression

The typing rules pin down the result types:

```text
(T-CtStmt)   Γ_ct ⊢ body : ()                            ⟹  CtStmt(...) : ()
(T-CtExpr)   Γ_ct ⊢ body : T   CtAvail(T)   ¬CtForbiddenType(T)
                                                          ⟹  CtExpr(...) : T
```

A `comptime` *statement* block always has type `()` (`TypePrim("()")`) and contributes no runtime statement; its only externally relevant effects are **declaration emission** and **diagnostics**. A `comptime` *expression* is replaced before Phase 3 by `CtLiteralize(cv)` of its evaluated value `cv` (or by the payload of a category-compatible `CtAst`).

`CtLiteralize` is total over the admissible value space and turns compile-time values back into ordinary literal expressions:

```text
CtLiteralize(CtPrim(UnitVal))            ⇓ TupleExpr([])          (* the () value *)
CtLiteralize(CtPrim(v))                  ⇓ Literal(ℓ)             (* v ≠ UnitVal *)
CtLiteralize(CtString(v))                ⇓ Literal(ℓ)
CtLiteralize(CtTuple([v_1,…,v_n]))       ⇓ TupleExpr([e_1,…,e_n])
CtLiteralize(CtArray([v_1,…,v_n]))       ⇓ ArrayExpr([e_1,…,e_n])
CtLiteralize(CtRecord(path, fields))     ⇓ RecordExpr(TypePath(path), …)
CtLiteralize(CtEnum(path, variant, ⊥))   ⇓ EnumLiteral(path ++ [variant], ⊥)
CtLiteralize(CtEnum(path, variant, CtTuplePayload(vs)))  ⇓ EnumLiteral(path ++ [variant], Paren(es))
CtLiteralize(CtEnum(path, variant, CtRecordPayload(io))) ⇓ EnumLiteral(path ++ [variant], Brace(io'))
CtLiteralize(CtAst(a))                   ⇓ AstPayloadOf(a)   if AstKindOf(a) = Expr
```

Worked example — fold a non-trivial computation into a constant at compile time. `comptime loop` is an array/slice unroller (§26.1.5), so an iteration count that is a procedure parameter uses an ordinary conditional `loop`, which is permitted inside compile-time bodies:

```ultraviolet
comptime procedure fibonacci(n: u32) -> u64 {
    var a: u64 = 0
    var b: u64 = 1
    var i: u32 = 0
    loop i < n {
        let next: u64 = a + b
        a = b
        b = next
        i = i + 1
    }
    return a
}

/// The 20th Fibonacci number, computed entirely in Phase 2.
public let FIB_20: u64 = comptime { fibonacci(20) }
```

After Phase 2, `comptime { fibonacci(20) }` is literalized to `6765`, and `fibonacci` itself does not survive into the runtime program.

#### 26.1.4 `comptime if`

```text
(T-CtIf)   Γ_ct ⊢ cond : bool   Γ_ct ⊢ then_blk : U   Γ_ct ⊢ else_blk : U
                                                       ⟹  CtIf(...) : U
```

The condition must evaluate at compile time to a `bool` (`E-CTE-0080` if not compile-time evaluable, `E-CTE-0081` if not of type `bool`). **For `comptime if`, only the selected branch becomes part of the expanded program.** The two `CtExpandExpr-CtIf-True/False` rules expand exactly one block; the other branch is discarded before typing, so a branch that would not type-check in the current configuration is harmless as long as it is the *unselected* one.

The `else` chain may continue with `comptime if` (an `else comptime if …` ladder) or terminate in a plain block. When `else_opt = ⊥`, the false branch defaults to the unit block `BlockExpr([], TupleExpr([]))` (`ElseBlock`).

```ultraviolet
/// Pick a buffer size that depends on a compile-time configuration constant.
public let RING_CAPACITY: usize = comptime if BUILD_PROFILE_IS_DEBUG {
    256
} else comptime if TARGET_IS_EMBEDDED {
    64
} else {
    4096
}
```

#### 26.1.5 `comptime loop`

```text
(T-CtLoopIter)
  Γ_ct ⊢ src : T_src   CtIterableType(T_src)
  (ty_opt = ⊥ ⇒ T_elem = ElemType(T_src))
  (ty_opt = T_ann ⇒ ElemType(T_src) <: T_ann ∧ T_elem = T_ann)
  Γ_ct, pat : T_elem ⊢ body : ()
                                                       ⟹  CtLoopIter(...) : ()
```

`comptime loop` is an **iterator-unrolling** form. The source must be a compile-time array or slice — `CtIterableType(T_src) ⇔ T_src = TypeArray(U,_) ∨ T_src = TypeSlice(U)`. A **range is not a `comptime loop` source**: a range expression has a `TypeRange*` type, which is neither `TypeArray` nor `TypeSlice`, so `comptime loop _ in 0..n` is ill-formed. The source is `E-CTE-0082` if not compile-time evaluable and `E-CTE-0083` if it is not a finite array/slice iterable. The optional `: type` after the pattern annotates the element type and requires `ElemType(T_src) <: T_ann`.

Semantics: **the source value MUST be finite and iteration order MUST equal the canonical element order of the source value.** Each iteration binds `pat` to the next element, expands the body, and the unrolled body statements are concatenated in iteration order (`CtLoopIterUnroll`). There is **no item-kind uniformity constraint** across iterations: if the loop body emits declarations, the emitted-item sequence is the concatenation of each iteration's emitted items in canonical iteration order.

```ultraviolet
/// Unroll a fixed dot product over a known dimension at compile time.
comptime procedure dotProduct(xs: [f64; 3], ys: [f64; 3]) -> f64 {
    var sum: f64 = 0.0
    comptime loop i: usize in [0, 1, 2] {
        sum += xs[i] * ys[i]
    }
    return sum
}
```

The loop source `[0, 1, 2]` is an array literal (an `ArrayExpr`, hence `TypeArray`), which is a valid `comptime loop` source.

#### 26.1.6 Compile-Time Procedures

```text
(T-CtProc)
  ∀ ⟨_, _, T⟩ ∈ params. CtAvail(T) ∧ ¬CtForbiddenType(T)
  CtAvail(ProcReturn(ret_opt)) ∧ ¬CtForbiddenType(ProcReturn(ret_opt))
  Γ_ct, params ⊢ body : ProcReturn(ret_opt)
                                                       ⟹  proc : wf
```

A `comptime procedure` is a Phase 2 binding only. The spec is emphatic on two constraints:

1. **`CtProc` declarations are Phase 2 bindings only and MUST NOT survive into the expanded Phase 3 module set.** They generate no runtime IR.
2. **Compile-time procedures MUST be callable only from compile-time contexts. Runtime expressions and runtime procedure bodies MUST NOT name, take the address of, store, or call a compile-time procedure** (`E-CTE-0034`).

Contracts on compile-time procedures use the ordinary `contract_clause` surface of the Contracts chapter (`|:` precondition `|=` postcondition; the postcondition may reference the returned value via the `@result` intrinsic). **At each compile-time call site, the precondition is evaluated before body execution and the postcondition is evaluated on the returned value. If any evaluated contract predicate is `false`, the call is ill-formed** (`E-CTE-0033`). Parameter and return types must satisfy `CtAvail`/`¬CtForbiddenType` (`E-CTE-0030`, `E-CTE-0031`); a body that uses prohibited constructs is `E-CTE-0032`.

```ultraviolet
/// Compute a compile-time alignment-up of `value` to a power-of-two `align`.
comptime procedure alignUp(value: usize, align: usize) -> usize
    |: align > 0 |= @result >= value
{
    let mask: usize = align - 1
    return (value + mask) & ~mask
}

public let HEADER_SIZE: usize = comptime { alignUp(37, 16) }   // 48
```

#### 26.1.7 Phase 2 Execution Order and Emission Visibility

A conforming implementation MUST satisfy:

1. `ComptimePass` evaluates modules in `Phase2ModuleOrder(P)` and **no other order**.
2. Within one module, compile-time sites execute in **source order** after earlier emitted declarations from that same module have been incorporated.
3. `CtProc` declarations do not survive into Phase 3.
4. `CtStmt` contributes no runtime statement.
5. `CtExpr` is replaced before Phase 3 by `CtLiteralize` or a category-compatible `CtAst`.
6. **Any item emitted at a Phase 2 site becomes visible immediately after that site** to later Phase 2 execution in the same module and to Phase 3 over the final expanded module set.

`CtExpandItem` returns a pair `⟨keep_items, emit_items⟩`: `keep_items` replaces the current item position and `emit_items` is inserted immediately *after* it. Any builtin that emits declarations appends them to `CtPendingEmits(Φ)`; before `CtExpandItem` returns, it **MUST transfer the accumulated pending emissions into `emit_items` in append order and clear the pending queue.**

**Cross-module emission is rejected.** Phase 2 execution **MUST NOT depend on declarations emitted by Phase 2 execution of a different module**; Phase-1 (source-present) declarations of other modules *may* be referenced. A dependency on another module's emitted declaration is `E-CTE-0090` / `CtExpand-CrossModule-Emit-Err`.

Lowering (§22.1.6): compile-time execution is complete before Phase 3 typing and Phase 4 lowering. No runtime IR is emitted for compile-time procedures, for compile-time statements, or for compile-time expressions after literalization or AST substitution. Phase 4 lowers only the expanded program.

---

### 26.2 Compile-Time Capabilities (§22.2)

Compile-time bodies receive a fixed set of capabilities, addressed by built-in identifiers. There is no new surface syntax beyond the `#emit` and `#files` attributes and these identifiers. The capability names are:

```text
CtCapName = { emitter, introspect, files, diagnostics }
CtCapType(emitter)     = TypePath(["TypeEmitter"])
CtCapType(introspect)  = TypePath(["Introspect"])
CtCapType(files)       = TypePath(["ProjectFiles"])
CtCapType(diagnostics) = TypePath(["ComptimeDiagnostics"])
```

A bare occurrence of one of these names in compile-time context parses as `Identifier(name)` (`Parse-CtCapRef`); capability method calls then use the ordinary call and method-call parsers, i.e. the `~>` operator.

#### 26.2.1 Availability

Authority is narrow and attribute-gated:

- **`introspect` and `diagnostics` are available in *every* compile-time context.**
- **`emitter` (`TypeEmitter`) is available only** inside a `comptime` form annotated with `#emit`, **or** inside the body of a derive target declaration.
- **`files` (`ProjectFiles`) is available only** inside a `comptime` form annotated with `#files`.

Using `emitter` without the capability is `E-CTE-0040`/`E-CTE-0250`; `#emit` on a non-compile-time form is `E-CTE-0041`. Using `files` without the capability is `E-CTE-0060`; `#files` on a non-compile-time form is `E-CTE-0061`.

The bindings injected at a node are exactly `CtCapBindings(node)`:

```text
CtCapBindings(node) =
    [⟨introspect, Introspect⟩, ⟨diagnostics, ComptimeDiagnostics⟩]
 ++ (⟨emitter, TypeEmitter⟩  if HasCtCap(node, TypeEmitter), else [])
 ++ (⟨files, ProjectFiles⟩   if HasCtCap(node, ProjectFiles), else [])
```

where `HasCtCap(node, Introspect)` and `HasCtCap(node, ComptimeDiagnostics)` hold whenever the node executes in Phase 2; `HasCtCap(node, TypeEmitter)` holds when, additionally, `#emit` applies or the node is a derive-target body; and `HasCtCap(node, ProjectFiles)` holds when `#files` applies.

#### 26.2.2 `TypeEmitter`

```text
TypeEmitter = { emit(ast: Ast) -> () }
```

`emitter~>emit(ast)` requires `ast` to have compile-time type `Ast::Item` or `Ast` (`E-CTE-0251` if the emitted AST is not an item; `E-CTE-0042`/`E-CTE-0252` if ill-formed after insertion; `E-CTE-0253` for a type error in emitted code). On emission, the fragment is **hygienized** (§26.4) and appended to `CtPendingEmits`. See §26.4 for a worked emission example.

#### 26.2.3 `Introspect`

```text
Introspect = {
  category(ty: Type)                    -> TypeCategory,
  fields(ty: Type)                      -> [FieldInfo],
  variants(ty: Type)                    -> [VariantInfo],
  states(ty: Type)                      -> [StateInfo],
  implements_form(ty: Type, form: Type) -> bool,
  type_name(ty: Type)                   -> string@Managed,
  module_path(ty: Type)                 -> string@Managed
}
```

The `fields`, `variants`, and `states` queries return slices (`[FieldInfo]`, `[VariantInfo]`, `[StateInfo]`). The full semantics of these queries are in §26.3 (Reflection). The info records have fixed shapes:

```text
FieldInfo   = { name: string@Managed, type: Type, visibility: string@Managed,
                index: usize, span: SourceSpan }
VariantInfo = { name: string@Managed, payload_kind: string@Managed,
                payload_types: [Type], field_names: [string@Managed], span: SourceSpan }
StateInfo   = { name: string@Managed, field_names: [string@Managed],
                method_names: [string@Managed], transition_names: [string@Managed],
                span: SourceSpan }
SourceSpan  = { file: string@Managed, start_line: usize, start_col: usize,
                end_line: usize, end_col: usize }
```

#### 26.2.4 `ProjectFiles`

```text
ProjectFiles = {
  read(path: string@View)       -> Outcome<unique string@Managed, IoError>,
  read_bytes(path: string@View) -> Outcome<unique bytes@Managed, IoError>,
  exists(path: string@View)     -> Outcome<bool, IoError>,
  list_dir(path: string@View)   -> Outcome<[string@Managed], IoError>,
  project_root()                -> string@Managed
}
```

All path-taking operations **MUST use project-root-relative paths**. The argument path:

- **MUST NOT be absolute** (`E-CTE-0063`);
- **MUST NOT contain `..` components that escape the project root** after normalization (`E-CTE-0062`);
- **MUST be resolved against a deterministic Phase 2 snapshot of project files**;
- if restriction fails, `read`/`read_bytes`/`exists`/`list_dir` **MUST return `IoError::InvalidPath`**.

The `IoError` variants are `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidPath`, `Busy`, `IoFailure`, and `DirectoryNotEmpty`. A path that is not found is also surfaced as `E-CTE-0064`. The success/error result is an `Outcome` enum value; its variants are `Value` and `Error`, matched with the `is Outcome::Value` / `is Outcome::Error` forms.

**Determinism guarantee.** Project-file reads MUST observe the `CtFiles(Φ)` snapshot captured at the start of Phase 2. **Host writes during compilation MUST NOT change the values** returned for the same restricted path.

```ultraviolet
#files
comptime {
    let manifest: Outcome<unique string@Managed, IoError> = files~>read("assets/shaders.list")
    if manifest is Outcome::Value(_) {
        diagnostics~>note("shader manifest located")
    } else {
        diagnostics~>warning("shader manifest missing; using built-in defaults")
    }
}
```

#### 26.2.5 `ComptimeDiagnostics`

```text
ComptimeDiagnostics = {
  error(message: string@View)   -> !,
  warning(message: string@View) -> (),
  note(message: string@View)    -> (),
  current_span()                -> SourceSpan,
  current_module()              -> string@Managed
}
```

`diagnostics~>error(msg)` has return type `!` (never) — it aborts compile-time evaluation and appends `⟨E-CTE-0070, Error, msg, sp⟩` at the current site span. `warning(msg)` appends `⟨W-CTE-0071, Warning, msg, sp⟩` and returns `()`; `note(msg)` appends a `Note` diagnostic (code `⊥`) and returns `()`. `current_span()` reifies the site's `SourceSpan`; `current_module()` returns the dotted module path text.

```ultraviolet
comptime procedure assertPowerOfTwo(n: usize) -> usize {
    comptime if (n & (n - 1)) != 0 {
        diagnostics~>error("expected a power of two")
    } else {
        return n
    }
}
```

Compile-time capabilities introduce no runtime object layout and no runtime symbol requirement beyond the declarations they emit during Phase 2.

---

### 26.3 Reflection (§22.3)

Reflection introspects types and members at compile time. It is **pure Phase 2 evaluation**: for one `CtMachine`, reflection results are immutable except for the visibility of declarations emitted earlier in the same Phase 2 order.

#### 26.3.1 The `Type::<T>` Literal

```ebnf
type_literal ::= "Type" "::" "<" type ">"
```

```text
(T-TypeLiteral)      Γ ⊢ T wf   ⟹   Γ_ct ⊢ TypeLiteralExpr(T) : TypePath(["Type"])
(CtEval-TypeLiteral) Γ ⊢ T wf   ⟹   CtEval(Ξ, Φ, TypeLiteralExpr(T)) ⇓ (CtType(T), Ξ, Φ)
```

`Type::<T>` reifies a well-formed type `T` into a compile-time `Type` value. An ill-formed `T` is `E-CTE-0410`; using `Type::<…>` in a runtime context is `E-CTE-0411`. The exact token sequence is `Type` `::` `<` `T` `>`; `::<` is a source spelling over `::` followed by `<`, not a single operator token and not a bare `<`.

#### 26.3.2 Categories

`introspect~>category(ty)` is valid for **any** well-formed `Type` value and returns a member of:

```text
TypeCategory = { Record, Enum, Modal, Primitive, Tuple, Array, Slice, Union,
                 Procedure, Reference, Dynamic, Opaque, Generic, String, Bytes, Range }
```

`CategoryOf` strips permission and refinement wrappers, maps `TypePath(p)`/`TypeApply(p, _)` to `Record`/`Enum`/`Modal`/`Generic` according to its declaration, and classifies the structural type constructors (tuples, arrays, slices, unions, procedures, references, dynamic, opaque, string, bytes, and ranges). A category query on an incomplete declaration is `E-CTE-0420`.

#### 26.3.3 Reflectability

The member queries require `Reflectable(ty)`. A nominal `record`/`enum`/`modal` declaration is reflectable only when it carries the `#reflect` attribute (`AttrByName(DeclOf(p), "reflect") ≠ []`); primitives, tuples, arrays, slices, and unions are always reflectable; type aliases are reflectable when their structural expansion is. A query against a non-reflectable or incomplete declaration is `E-CTE-0053`/`E-CTE-0470`.

#### 26.3.4 Member Queries

- **`introspect~>fields(ty)`** — valid only when `CategoryOf(ty) = Record` and `Reflectable(ty)`; returns `[FieldInfo]` in **declaration order**. Applying it to a non-record type is `E-CTE-0050`/`E-CTE-0430`.
- **`introspect~>variants(ty)`** — valid only when `CategoryOf(ty) = Enum` and `Reflectable(ty)`; returns `[VariantInfo]` in **declaration order**. `payload_kind` is `"unit"`, `"tuple"`, or `"record"`. Applying it to a non-enum type is `E-CTE-0051`/`E-CTE-0440`.
- **`introspect~>states(ty)`** — valid only when `CategoryOf(ty) = Modal` and `Reflectable(ty)`; returns `[StateInfo]` in **declaration order** (which, per the style guide, is lifecycle order). Applying it to a non-modal type is `E-CTE-0052`/`E-CTE-0450`.

For a generic declaration, field/variant/state types are returned with the monomorphizing substitution `θ` (from `DefaultArgs(params_gen, args)`) already applied to the supplied type arguments.

#### 26.3.5 Type Predicates and Names

- **`introspect~>implements_form(ty, form)`** evaluates **the same class-satisfaction judgment used by Phase 3 typing** after substituting any monomorphized type arguments of `ty`, and returns `bool`.
- **`introspect~>type_name(ty)`** returns the rendered type name (`TypeRender(T)`), a `string@Managed`.
- **`introspect~>module_path(ty)`** returns the dotted module path of `ty`'s declaration as a `string@Managed` (empty for non-nominal types).

#### 26.3.6 Worked Example

This example marks a record reflectable, reifies it with `Type::<…>`, branches on its category with `comptime if`, and iterates the reflected field slice with a `comptime loop` (a slice is a valid `comptime loop` source):

```ultraviolet
#reflect
public record Particle {
    public position: f32,
    public velocity: f32,
    public mass: f32
}

#emit
comptime {
    let particle_ty: Type = Type::<Particle>
    comptime if introspect~>category(particle_ty) == TypeCategory::Record {
        comptime loop info: FieldInfo in introspect~>fields(particle_ty) {
            diagnostics~>note(info.name~>as_view())
        }
    } else {
        diagnostics~>error("Particle must be a record")
    }
}
```

Reflected `Type` values and reflection result slices **do not survive into Phase 4** unless reified into emitted declarations or literalized constants.

---

### 26.4 Quote, Splice, and Emission (§22.4)

Quotation builds `Ast` values from source-like fragments; splicing substitutes compile-time values into those fragments; emission inserts an `Ast::Item` into the expanded program.

#### 26.4.1 Exact Syntax

```ebnf
quote_expr     ::= "quote" "{" quoted_content "}"
quote_type     ::= "quote" "type" "{" type "}"
quote_pattern  ::= "quote" "pattern" "{" pattern "}"
quoted_content ::= expression | statement | top_level_item
splice_expr    ::= "$" "(" expression ")"
splice_ident   ::= "$" identifier
```

> *(Splice forms parse in any primary-expression position; use outside quoted content is rejected statically — `E-CTE-0233`. Within quoted content, `splice_ident` is additionally admitted wherever `identifier` is admitted.)*

`quote { … }`, `quote type { … }`, and `quote pattern { … }` are valid **only in compile-time contexts** (`E-CTE-0221` otherwise). The body of a raw `quote { … }` is exactly one `expression`, `statement`, or `top_level_item`; `quote type { … }` wraps exactly one `type`; `quote pattern { … }` wraps exactly one `pattern`.

The AST kinds are `AstKind = { Expr, Stmt, Item, Type, Pattern }`, and a value is `Ast = AstNode(kind, payload, span, hygiene)`.

#### 26.4.2 Quote Kind Resolution

A raw `quote { … }` carries no explicit kind, so its category is resolved from the expected type or from unique parseability:

```text
ResolveQuoteKind(QuoteNode(kind, _, _), T_exp)  = kind   if kind ≠ ⊥
ResolveQuoteKind(QuoteNode(⊥, _, _),    T_exp)  = kind   if ExpectedAstKind(T_exp) = kind ∧ kind ≠ ⊥
ResolveQuoteKind(QuoteNode(⊥, body, _), T_exp)  = kind   if ExpectedAstKind(T_exp) = ⊥
                                                          ∧ kind is the unique member of {Expr, Stmt, Item}
                                                            for which ParseQuotedBody(kind, body) succeeds
```

`ExpectedAstKind` maps the static types `Ast::Expr`, `Ast::Stmt`, `Ast::Item`, `Ast::Type`, `Ast::Pattern` to their kinds, and `Ast` to `⊥` (unconstrained). **Quoted content MUST be syntactically valid in the resolved category.** If `ResolveQuoteKind` is undefined — invalid syntax or an unresolvable ambiguity among `{Expr, Stmt, Item}` — the quote is ill-formed (`E-CTE-0220`). The `Type` and `Pattern` kinds are fixed by the `quote type` / `quote pattern` forms and never participate in raw-quote ambiguity resolution.

#### 26.4.3 Splicing Rules

`$(e)` and `$ident` are valid **only inside a quoted token slice** (`E-CTE-0233` otherwise). Both are `$` decorator spellings over ordinary following tokens. The compile-time type of the splice source MUST satisfy `SpliceCompat` for the surrounding quoted position (`E-CTE-0230` otherwise; `E-CTE-0231` if the splice expression is not compile-time evaluable):

```text
SpliceCompat(Expr, T)       ⇔ T = Ast ∨ T = Ast::Expr ∨ CtLiteralType(T)
SpliceCompat(Stmt, T)       ⇔ T = Ast::Stmt ∨ T = Ast::Expr
SpliceCompat(Item, T)       ⇔ T = Ast::Item
SpliceCompat(Type, T)       ⇔ T = Ast::Type ∨ T = Type
SpliceCompat(Pattern, T)    ⇔ T = Ast::Pattern
SpliceCompat(Identifier, T) ⇔ T = string@Managed ∨ T = string@View
```

`CtLiteralType(T)` holds for the literalizable types: primitives other than `!`, `@View`/`@Managed` strings, and tuples/arrays/records/enums recursively built from them.

**`$ident` is an identifier-position splice only.** `SpliceIdentNode` MAY occur **only** in:

- identifier expressions,
- identifier-pattern bindings,
- typed-pattern bindings,
- `using … as` alias names,
- `region as` aliases,
- procedure or method **parameter** bindings.

It **MUST NOT** occur in structural identifier positions — **module or type path segments, field labels, variant names, type-parameter names, item declaration names, or modal state names.** In particular, you **cannot splice a declaration's own name** (a procedure name, record name, etc.) with `$ident`; declaration names in quoted item fragments are written literally and hygienized. In every other quoted position, including quoted *type* position, splicing **MUST use `$(e)`**. Ordinary syntax retains precedence where it already uses `$`: in `quote type { $IO }`, `$IO` parses as `TypeDynamic(["IO"])` (a dynamic type), **not** as a splice. An invalid identifier string in a splice is `E-CTE-0232`.

**Unhygienic identifier splices.** If a *string-valued* splice occupies one of the allowed identifier positions, the resulting identifier is **intentionally unhygienic** and binds in the emission environment.

`RenderSplice` defines how each splice value becomes a payload:

```text
RenderSplice(Expr, cv)       ⇓ payload  iff (cv = CtAst(a) ∧ AstKindOf(a)=Expr ∧ payload=AstPayloadOf(a))
                                          ∨ (cv ≠ CtAst(_) ∧ CtLiteralize(cv) ⇓ payload)
RenderSplice(Stmt, cv)       ⇓ payload  iff cv = CtAst(a) ∧ AstKindOf(a) ∈ {Stmt, Expr} ∧ payload=AstPayloadOf(a)
RenderSplice(Item, cv)       ⇓ payload  iff cv = CtAst(a) ∧ AstKindOf(a)=Item ∧ payload=AstPayloadOf(a)
RenderSplice(Type, cv)       ⇓ payload  iff (cv = CtAst(a) ∧ AstKindOf(a)=Type ∧ payload=AstPayloadOf(a))
                                          ∨ (cv = CtType(T) ∧ payload = T)
RenderSplice(Pattern, cv)    ⇓ payload  iff cv = CtAst(a) ∧ AstKindOf(a)=Pattern ∧ payload=AstPayloadOf(a)
RenderSplice(Identifier, cv) ⇓ payload  iff cv = CtString(name) ∧ payload = Identifier(name)
```

`QuoteBuild` evaluates splice expressions in **left-to-right source order**, renders each by `RenderSplice`, substitutes into the quoted payload, and the result becomes the payload of the returned `Ast`.

#### 26.4.4 Hygiene

A quoted `Ast` is hygienized at insertion via `HygienizeAst`, which MUST satisfy:

1. **Any capture from the quote site resolves to the same binding after emission.**
2. **Any binder introduced by hygienic quoted content** — including top-level declaration names in quoted item fragments — **MUST NOT capture names from the emission site** unless the splice was string-valued in identifier position.
3. **Fresh hygienic marks are deterministic functions of `quote_site`, `emit_site`, and the input counter `n`.**

A reference inside the fragment that resolves to a hygienic binder introduced by that same fragment before emission MUST resolve to the renamed binding after emission. For `using` and `import`, **only explicit alias names are hygienic binders**; unaliased imported names are preserved as written. Hygiene failures are `E-CTE-0240` (capture no longer resolves) and `E-CTE-0241` (renaming collision after an unhygienic splice).

#### 26.4.5 Emission

`emitter~>emit(ast)` is well-formed only when `emitter` has compile-time type `TypeEmitter` and `ast` has compile-time type `Ast::Item` or `Ast`. Emission order is:

1. **derive-generated emissions** required by §26.5 for the current declaration;
2. **explicit `emitter~>emit` calls** in source order.

An `Ast` value used in a runtime context is `E-CTE-0210`. No runtime representation of `Ast` survives unless explicitly embedded by emitted declarations.

#### 26.4.6 Worked Example

The procedure *name* in a quoted item is written literally (a declaration name is a structural identifier position and may not be spliced). The `$(field_ty)` splice substitutes a `Type` value into type position via `RenderSplice(Type, CtType(usize))`. The `$param_name` splices occupy an allowed identifier position — a *parameter binding* in the signature and an *identifier expression* in the body — so a string-valued splice is legal there and binds in the emission environment:

```ultraviolet
/// Emit a typed identity accessor whose parameter name and type are spliced in.
#emit
comptime {
    let param_name: string@Managed = "frame"
    let field_ty: Type = Type::<usize>

    let accessor: Ast::Item = quote {
        public procedure rememberFrame($param_name: $(field_ty)) -> $(field_ty) {
            return $param_name
        }
    }
    emitter~>emit(accessor)
}
```

Here the emitted item is named `rememberFrame` literally; `$param_name` (a `string@Managed`) renders to `Identifier("frame")` in both the parameter-binding and identifier-expression positions, and `$(field_ty)` renders the `Type` value `usize` in each type position.

---

### 26.5 Derive Targets and Contracts (§22.5)

A *derive target* is a named, reusable compile-time code generator. Attaching `#derive(Name)` to a type declaration runs the target's body once over that declaration, emitting items.

#### 26.5.1 Exact Syntax

```ebnf
derive_attribute    ::= "#" "derive" "(" derive_target_list ")"
derive_target_list  ::= identifier ("," identifier)*
derive_target_decl  ::= "derive" "target" identifier "(" "target" ":" "Type" ")" derive_contract_opt block_expr
derive_contract_opt ::= "|:" derive_clause ("," derive_clause)*
derive_clause       ::= "emits" identifier | "requires" identifier
```

A `derive target` declaration has the literal signature `( target : Type )` — the parameter name is the keyword-like identifier `target`, its type is `Type`. The optional contract uses `|:` followed by comma-separated `requires`/`emits` clauses naming classes. An invalid derive-target signature is `E-CTE-0322`. (The `#derive(…)` attribute itself is parsed by the ordinary attribute parser; this section interprets the `derive` attribute name and its argument list.)

#### 26.5.2 Validity and Bindings

`#derive(…)` is valid **only on `record`, `enum`, and `modal` declarations** (`E-CTE-0311` otherwise). Every derive target name MUST resolve to **exactly one** visible `derive target` declaration (`E-CTE-0310` for unknown name; a duplicate name in one `#derive(…)` attribute is `E-CTE-0312`).

Within a derive target body, the available bindings are exactly:

```text
target      : Type      (* = TargetTypeOf(D) = TypePath(ItemPath(D)) *)
emitter     : TypeEmitter
introspect  : Introspect
diagnostics : ComptimeDiagnostics
```

**The body executes under the same restrictions as any other compile-time procedure body** (`E-CTE-0320` if violated; `E-CTE-0341` if it panics).

#### 26.5.3 Derive Contracts and Ordering

`requires` and `emits` clauses **participate only in derive ordering and validation against the annotated declaration's explicit `implements` list. They do not add or remove class implementations for `D`.** A clause referencing an unknown class is `E-CTE-0321`.

Validation: before executing derive target `name` for type `D`, every class in `DeriveReqs(name)` and every class in `DeriveEmits(name)` **MUST belong to `DeclaredImplNames(D)`** — the trailing names of the classes in `D`'s explicit `implements` (`<:`) list (`E-CTE-0330` for a missing required class, `E-CTE-0331` for a missing emitted class).

Ordering: for one annotated declaration `D`, derive execution order is `DeriveOrder(D)` — the **stable topological order** of the dependency graph `DeriveGraph(D)` whose vertices are the `DeriveRequest(D, name)` and whose edge `name_i -> name_j` holds when `DeriveReqs(name_i) ∩ DeriveEmits(name_j) ≠ ∅` (i.e., `name_i` requires a class that `name_j` emits, so `name_j` must run first). **If multiple derive targets are incomparable in that graph, source order in `#derive(…)` is the tie-breaker** (`StableTopologicalOrder`). A cycle is `E-CTE-0340`.

#### 26.5.4 Execution Semantics

`DeriveTargetDecl` is a **compile-time-only item**; it is visible to later derive lookup in the same Phase 2 module order and **MUST NOT survive into the expanded Phase 3 module set** (`CtExpandItem(Ξ, Φ, dt) ⇓ (⟨[], []⟩, Ξ, Φ)`). **Each derive target executes exactly once per annotated declaration, immediately after the annotated declaration has been introduced and before any later source item in the same module is executed.** The expansion of an annotated declaration `D` keeps `D` and inserts the derive emissions after it (`CtExpandItem(Ξ, Φ, D) ⇓ (⟨[D], emits⟩, …)`).

**If a derive target signals `diagnostics~>error`, panics, or emits ill-formed declarations, Phase 2 fails and compilation is rejected.** Derive targets introduce no runtime dispatch or metadata; their only lowering consequence is the presence of the declarations emitted during Phase 2.

#### 26.5.5 Worked Example

```ultraviolet
/// A derive target that emits a textual `describe` accessor for any record.
public derive target Describe(target: Type) {
    comptime if introspect~>category(target) != TypeCategory::Record {
        diagnostics~>error("Describe applies only to record types")
    } else {
        let type_name: string@Managed = introspect~>type_name(target)
        let describe_proc: Ast::Item = quote {
            public procedure describe(handle: $(target)) -> string@View {
                return $(type_name)
            }
        }
        emitter~>emit(describe_proc)
    }
}

#derive(Describe)
public record DeviceHandle {
    public device_id: u64,
    public generation: u32
}
```

The `Describe` target runs once over `DeviceHandle`. `$(target)` splices the target type into the parameter type position; `$(type_name)` is an **expression-position** splice of a `string@Managed` value (`SpliceCompat(Expr, string@Managed)` via `CtLiteralType`), so it renders to a string literal — note that this is `$(type_name)`, not `$type_name`, because `$ident` is an identifier-position splice and would instead produce a *variable reference* named by the string. The emitted `describe` procedure is inserted immediately after the `DeviceHandle` declaration and is visible to the rest of Phase 2 and to Phase 3.

A pair of derive targets that order themselves through contracts:

```ultraviolet
/// Emits the `Encode` implementation; must run before any target that requires it.
public derive target Codec(target: Type) |: emits Encode {
    let body: Ast::Item = quote {
        public procedure encode(handle: $(target)) -> bytes@Managed {
            return handle.payload
        }
    }
    emitter~>emit(body)
}

/// Requires `Encode` to already be implemented, so `Codec` is ordered first.
public derive target Checksum(target: Type) |: requires Encode {
    let body: Ast::Item = quote {
        public procedure checksum(handle: $(target)) -> u32 {
            return 0
        }
    }
    emitter~>emit(body)
}

#derive(Checksum, Codec)
public record Packet <: Encode {
    public payload: bytes@Managed
}
```

Although `Checksum` is listed first in `#derive(Checksum, Codec)`, `Checksum` *requires* `Encode` and `Codec` *emits* `Encode`, so the edge `Checksum -> Codec` forces `Codec` to execute first. The required and emitted class `Encode` must appear in `Packet`'s explicit `implements` list — here `<: Encode` — or compilation is rejected with `E-CTE-0330`/`E-CTE-0331`.

---

### 26.6 Idioms & Best Practices

- **Express correctness in the code, not in comments.** Prefer compile-time contracts on `comptime procedure` declarations (`|: pre |= post`) over defensive runtime checks, and prefer `comptime if`/`comptime loop` over generating dead runtime branches. Move what *can* be decided at compile time into Phase 2.
- **Keep authority narrow.** `introspect` and `diagnostics` are always available, but `emitter` and `files` are attribute-gated. Annotate a `comptime` block with `#emit` *only* when it actually emits, and with `#files` *only* when it reads project files. Do not over-grant capability by attaching attributes speculatively.
- **Name per the style guide.** Compile-time procedures and derive-emitted procedures are still procedures: use `camelCase` (`alignUp`, `assertPowerOfTwo`, `describe`, `rememberFrame`). Derive target identifiers are type-like generators and read naturally in `PascalCase` (`Describe`, `Codec`, `Checksum`). Types reified via `Type::<…>` keep `PascalCase`. Constants produced by `comptime { … }` use `SCREAMING_SNAKE` (`FIB_20`, `HEADER_SIZE`, `RING_CAPACITY`). Local variables and parameters inside comptime bodies use `snake_case` (`type_name`, `field_ty`, `param_name`).
- **Document generators.** Public derive targets and public compile-time procedures must carry `///` documentation covering purpose, preconditions (including required `implements` classes), what is emitted, and failure modes; public modules that expose them need `//!` module docs.
- **Reach for reflection over hand-maintained lists.** When you need per-field or per-variant code, drive it from `introspect~>fields`/`variants`/`states` (declaration order is guaranteed) inside a `comptime loop` over the returned slice, rather than duplicating the member list. Mark the subject type `#reflect`.
- **Prefer derive targets to scattered `#emit` blocks** for reusable, type-driven generation. A `derive target` is named, contract-checked, ordered, and runs exactly once per subject — strictly better than copy-pasted emission logic.
- **Use `comptime loop` for arrays and slices only.** Iterate a known array/slice value (including the slices returned by reflection). For a count that depends on a runtime-supplied compile-time parameter, use an ordinary conditional `loop` inside the comptime body — a range is not a `comptime loop` source.
- **Keep emission hygienic by default.** Use `$(e)` splices and let hygiene rename generated binders. Never try to splice a declaration's own name — declaration names are structural identifier positions and are forbidden to `$ident`; write them literally. Reserve string-valued identifier splices (`$some_string`) for the deliberate, allowed cases — parameter bindings, identifier patterns, identifier expressions, and `using`/`region` aliases — where the generated name must bind in the emission environment, and document why.
- **Respect determinism.** `files` reads see a fixed Phase 2 snapshot; never assume a file written during compilation is observable. Keep generation a pure function of the snapshot, the annotated declaration, and reflection. Keep generation-dependency chains within a single module — cross-module emitted-declaration dependencies are rejected.

### 26.7 Pitfalls & Diagnostics

- **Calling a `comptime procedure` from runtime code.** Compile-time procedures exist only in Phase 2; naming, storing, address-taking, or calling one from a runtime body is `E-CTE-0034`. Wrap the result in a `comptime { … }` expression (which literalizes to a constant) instead.
- **Forbidden types leaking into compile-time position.** A capability-bearing, modal-state, dynamic, `Ptr`, raw-pointer, function-typed, or `Context` value used as a compile-time variable, parameter, return, or expression triggers `E-CTE-0010`/`E-CTE-0011`/`E-CTE-0012`/`E-CTE-0021`/`E-CTE-0030`/`E-CTE-0031`. Reduce to admissible `CtValue` shapes (primitives, strings, bytes, `Type`, `Ast`, tuples, arrays, slices, plain records/enums) first. Note that the `Outcome` results returned by `files~>read` and friends are enum values consumed by `is Outcome::Value`/`is Outcome::Error` inside the comptime body; do not try to make an `Outcome` itself a compile-time variable type.
- **Prohibited runtime constructs in compile-time bodies.** `region`/`frame`, key blocks, the full concurrency set (`parallel`, `spawn`, `dispatch`, `wait`, `yield`, `yield from`, `sync`, `race`, `all`), `transmute`, raw deref, `unsafe`, and FFI calls are all `E-CTE-0020`. Compute the data at compile time; perform side effects at runtime.
- **`comptime if`/`loop` source not compile-time evaluable.** A non-evaluable condition is `E-CTE-0080`; a non-`bool` condition is `E-CTE-0081`; a non-evaluable loop source is `E-CTE-0082`; an infinite or non-array/slice source (including a range) is `E-CTE-0083`. `comptime loop` iterates only finite arrays/slices in canonical order.
- **Missing capability attributes.** `emitter` without `#emit` (or a derive body) is `E-CTE-0040`/`E-CTE-0250`; `files` without `#files` is `E-CTE-0060`. `#emit`/`#files` on a non-compile-time form is `E-CTE-0041`/`E-CTE-0061`.
- **Emitting a non-item.** `emitter~>emit` requires `Ast::Item` or `Ast`; emitting an expression/statement fragment is `E-CTE-0251`, an emitted AST that is otherwise ill-formed is `E-CTE-0042`, a fragment that fails well-formedness after insertion is `E-CTE-0252`, and a type error in emitted code is `E-CTE-0253`.
- **Splice placement and category errors.** A splice outside a quote is `E-CTE-0233`; a splice whose type violates `SpliceCompat` is `E-CTE-0230`; a non-evaluable splice is `E-CTE-0231`; an invalid identifier string is `E-CTE-0232`. Remember `$ident` is *identifier-position only* and never valid in path segments, field labels, variant names, type-parameter names, **declaration names**, or modal state names — use `$(e)` everywhere else, and recall that `$IO` in `quote type { … }` is a dynamic type, not a splice. To splice a string *value* into expression position (e.g. a returned string), use `$(s)`, not `$s`.
- **Ambiguous or invalid quotes.** Quoted content that does not parse in its category, or that is ambiguous among `Expr`/`Stmt`/`Item` with no type to disambiguate, is `E-CTE-0220`. Annotate the binding with `Ast::Expr`/`Ast::Stmt`/`Ast::Item` to resolve the kind; `Ast::Type`/`Ast::Pattern` come from the `quote type`/`quote pattern` forms. A quote form outside a compile-time context is `E-CTE-0221`.
- **Hygiene failures.** A capture that no longer resolves at the emission site is `E-CTE-0240`; a renaming collision introduced by an unhygienic string-identifier splice is `E-CTE-0241`. Prefer hygienic `$(e)` splices to avoid both.
- **Reflection misuse.** A category query on an incomplete declaration is `E-CTE-0420`; `fields`/`variants`/`states` on the wrong category are `E-CTE-0050`/`E-CTE-0430`, `E-CTE-0051`/`E-CTE-0440`, `E-CTE-0052`/`E-CTE-0450`; a member or predicate query against a non-reflectable or incomplete declaration is `E-CTE-0053`/`E-CTE-0470`. Mark nominal subject types `#reflect`. `Type::<…>` on an ill-formed type is `E-CTE-0410`; used in a runtime context it is `E-CTE-0411`.
- **Derive misuse.** `#derive(…)` on a non-`record`/`enum`/`modal` is `E-CTE-0311`; an unknown target name is `E-CTE-0310`; a duplicate target in one attribute is `E-CTE-0312`; an invalid `derive target` signature is `E-CTE-0322`; a `requires`/`emits` class missing from the subject's explicit `implements` list is `E-CTE-0330`/`E-CTE-0331`; a contract naming an unknown class is `E-CTE-0321`; a cyclic dependency is `E-CTE-0340`. A derive body that violates compile-time restrictions is `E-CTE-0320`; one that calls `diagnostics~>error`, panics (`E-CTE-0341`), or emits ill-formed code fails Phase 2 outright.
- **Cross-module emission dependency.** Referring from one module's compile-time form to a declaration *emitted* by another module's Phase 2 execution is unsupported and rejected as `E-CTE-0090` (`CtExpand-CrossModule-Emit-Err`). Reference only source-present declarations of other modules; keep generation-dependency chains within a single module.
- **Compile-time error reporting and user diagnostics.** `diagnostics~>error` emits `E-CTE-0070` and aborts (type `!`); `diagnostics~>warning` emits `W-CTE-0071`; `diagnostics~>note` emits an unnumbered `Note`. Use `error` for hard failures (it ends evaluation) and `warning`/`note` for advisory output that lets Phase 2 continue.
