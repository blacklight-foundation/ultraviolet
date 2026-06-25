## 5. Names, Scopes & Visibility

Every identifier you write in Ultraviolet — a procedure name, a type name, a local variable, a module path segment — is resolved against a precise, layered model of *scopes* and *visibility*. This chapter is the definitive account of how the compiler decides what a name means, where you are allowed to introduce a name, what the `::` operator resolves to, and which declarations you are permitted to reference across module and assembly boundaries. The rules here are drawn from the specification's §7 *Name Resolution and Visibility*, together with the module-system judgments of §11.5.4 that §7 consumes.

Name resolution operates over a context the spec writes as `Γ` (the resolution context). Understanding `Γ` and its scope stack is the key to everything that follows, so we begin there.

### 5.1 The Scope Context and Identifier Kinds (§7.1)

#### 5.1.1 The resolution context `Γ`

Resolution is performed against a four-part context:

```text
Γ = ⟨P, Σ, m, S⟩
Project(Γ) = P
ResCtx(Γ) = ⟨Σ, m⟩
CurrentModule(Γ) = m
Scopes(Γ) = S
```

- `P` is the whole **project** being compiled.
- `Σ = ⟨Σ.Mods, Σ.Types, Σ.Classes⟩` is the global signature environment: the list of AST modules (`Σ.Mods`), plus partial maps `Σ.Types : Path ⇀ TypeDecl` and `Σ.Classes : Path ⇀ ClassDecl`.
- `m` is the **current module** (`CurrentModule(Γ)`), i.e. the module whose source is presently being resolved.
- `S` is the **scope stack** (`Scopes(Γ)`).

#### 5.1.2 The scope stack

A scope is a finite map from an *identifier key* to an *entity*:

```text
S : IdKey ⇀ Entity
ScopeKey(S) ⇔ dom(S) ⊆ {IdKey(x) | x ∈ Identifier}
```

An `IdKey` is the NFC-normalized form of an identifier (`IdKey(s) = NFC(s)`), so two identifiers that differ only in Unicode normalization are the *same* name. Identifier equality is exactly key equality: `IdEq(s_1, s_2) ⇔ IdKey(s_1) = IdKey(s_2)`.

The scope stack is ordered from innermost to outermost:

```text
Scopes(Γ) = [S_1, …, S_k, S_proc, S_module, S_universe]    (k ≥ 0)
LocalScopes(Γ) = [S_1, …, S_k]
ProcScope(Γ)   = S_proc
ModuleScope(Γ) = S_module
UniverseScope(Γ) = S_universe
```

The layers, from innermost outward, are:

1. **Local scopes** `S_1 … S_k` — one per nested block, loop body, `if is` / `if` case arm, `race` arm, `dispatch` body, etc. (each `PushScope` adds one; see §5.7).
2. **Procedure scope** `S_proc` — parameters, `self`, and type parameters of the enclosing procedure/method.
3. **Module scope** `S_module` — every top-level name collected from the current module (§5.5).
4. **Universe scope** `S_universe` — the outermost layer, holding the built-in protected names.

#### 5.1.3 Entities

Each scope binding maps a key to an `Entity`:

```text
EntityKind   = {Value, Type, Class, ModuleAlias}
EntitySource = {Decl, Using, RegionAlias}
Entity = ⟨kind, origin_opt, target_opt, source⟩
origin_opt ∈ ModulePath ∪ {⊥}
target_opt ∈ Identifier ∪ {⊥}
```

- **`kind`** classifies the entity: a `Value` (procedures, statics, locals, parameters, foreign procedures), a `Type` (records, enums, modals, type aliases), a `Class`, or a `ModuleAlias` (introduced by `import`).
- **`origin_opt`** is the module the entity ultimately lives in, or `⊥` for a purely local binding (a local, parameter, or type parameter).
- **`target_opt`** is the original name when the binding is an alias (e.g. a `using ... as ...`), or `⊥`.
- **`source`** records how the binding arose: a direct `Decl`, a `Using` re-export, or a `RegionAlias`.

Convenience predicates over an entity's kind/source are used throughout the chapter:

```text
ValueKind(ent)   ⇔ ent.kind = Value
TypeKind(ent)    ⇔ ent.kind = Type
ClassKind(ent)   ⇔ ent.kind = Class
ModuleKind(ent)  ⇔ ent.kind = ModuleAlias
RegionAlias(ent) ⇔ ent.source = RegionAlias
```

#### 5.1.4 The universe scope and reserved names

The outermost scope is fixed for every module:

```text
UniverseBindings =
    { IdKey(x) ↦ ⟨Type, ⊥, ⊥, Decl⟩ | x ∈ UniverseProtected }
  ∪ { IdKey(`ultraviolet`) ↦ ⟨ModuleAlias, `ultraviolet`, ⊥, Decl⟩ }
S_universe = UniverseBindings
```

`UniverseProtected` is the set of built-in primitive, special, and async type names. They fall into three named categories:

```text
PrimTypeNames    = {i8, i16, i32, i64, i128, u8, u16, u32, u64, u128,
                    f16, f32, f64, bool, char, usize, isize}
SpecialTypeNames = {Self, Drop, Bitcopy, Clone, Eq, Hash, Hasher, Iterator,
                    Discrete, FfiSafe, string, bytes, Modal, Region, RegionOptions,
                    CancelToken, Context, TestAuthority, System, IO, HeapAllocator,
                    Network, ExecutionDomain, CpuSet, Priority, Reactor, Time,
                    MonotonicTime, WallTime, Duration, MonotonicInstant,
                    UtcInstant, TimeError, Outcome}
AsyncTypeNames   = {Async, Future, Sequence, Stream, Pipe, Exchange, Tracked}
```

The key sets derived from these are `PrimTypeKeys`, `SpecialTypeKeys`, and `AsyncTypeKeys` (each `{IdKey(x) | x ∈ …}`). Because `S_universe` is always the *outermost* scope, every one of these names is already bound before any of your code runs. Attempting to declare or alias one at module scope (or any inner scope) is therefore a name-reuse error via `(Intro-Outer-Err)` (§5.2), not a special case. The spec calls this out explicitly: `Drop`, `Bitcopy`, `Clone`, `FfiSafe`, and `GpuSafe` are reserved foundational class names included in `SpecialTypeNames`; reuse of these names at any scope is an error because `UniverseBindings` is the outermost scope and contains them.

The single `ModuleAlias` binding `ultraviolet` reserves the standard-library root.

#### 5.1.5 Reserved-identifier predicates

Two identifier shapes are reserved for the compiler and the standard library:

```text
ReservedGen(x)           ⇔ Prefix(IdKey(x), IdKey(`gen_`))
ReservedUltraviolet(x)   ⇔ IdEq(x, `ultraviolet`)
ReservedId(x)            ⇔ ReservedGen(x) ∨ ReservedUltraviolet(x)
ReservedModulePath(path) ⇔ (|path| ≥ 1 ∧ IdEq(path[0], `ultraviolet`))
                          ∨ (∃ i. ReservedGen(path[i]))
```

- Any identifier beginning with `gen_` is reserved for generated code.
- The identifier `ultraviolet` is reserved for the standard-library root.
- A module path is reserved if its first segment is `ultraviolet` or any segment carries the `gen_` prefix.

`Prefix(s, p)` is defined byte-wise on the normalized keys: `BytePrefix(p, s) ⇔ ∃ r. s = p ++ r`, and `Prefix(s, p) ⇔ BytePrefix(p, s)`.

#### 5.1.6 Keyword keys

Finally, a key is a *keyword key* when it equals the NFC form of a reserved lexeme:

```text
KeywordKey(n) ⇔ ∃ s. n = IdKey(s) ∧ Keyword(s)
Keyword(s) ⇔ s ∈ Reserved
```

The full `Reserved` set (the keywords you may never use as identifiers) is:

```text
Reserved = {all, as, break, class, comptime, continue, copy, derive, dispatch,
            else, enum, false, defer, frame, from, if, imm, import, internal,
            let, loop, modal, move, mut, null, parallel, private, procedure,
            public, quote, race, record, region, return, shared, spawn, sync,
            transition, transmute, true, type, unique, unsafe, var, widen, using,
            yield, const, override}
```

The set of future-reserved words is empty (`FutureReserved = ∅`). `KeywordKey` is what module-name validation checks against (§5.2).

### 5.2 Name Introduction and Module Validation (§7.2)

Introducing a name into the current scope is governed by the `Intro` judgment. Write the scope stack as a current scope plus the rest:

```text
Scopes(Γ) = [S_cur] ++ Γ_out
InScope(S, x) ⇔ IdKey(x) ∈ dom(S)
InOuter(Γ, x) ⇔ ∃ S ∈ Γ_out. InScope(S, x)
```

#### 5.2.1 The `Intro` rules

```text
(Intro-Ok)
¬ InScope(S_cur, x)   ¬ InOuter(Γ, x)   ¬ ReservedId(x)
Scopes(Γ') = [S_cur[IdKey(x) ↦ ent]] ++ Γ_out
Project(Γ') = Project(Γ)   ResCtx(Γ') = ResCtx(Γ)
─────────────────────────────────────────────────────
Γ ⊢ Intro(x, ent) ⇓ Γ'
```

A new binding is admitted only when the name is **not already in the current scope**, **not visible in any outer scope**, and **not reserved**. The four error rules cover the violations:

```text
(Intro-Dup)
InScope(S_cur, x)   c = Code(Intro-Dup)
──────────────────────────────────────
Γ ⊢ Intro(x, ent) ⇑ c

(Intro-Outer-Err)
¬ InScope(S_cur, x)   InOuter(Γ, x)   c = Code(Intro-Outer-Err)
──────────────────────────────────────────────────────────────
Γ ⊢ Intro(x, ent) ⇑ c

(Intro-Reserved-Gen-Err)
ReservedGen(x)   c = Code(Intro-Reserved-Gen-Err)
─────────────────────────────────────────────────
Γ ⊢ Intro(x, ent) ⇑ c

(Intro-Reserved-Ultraviolet-Err)
ReservedUltraviolet(x)   c = Code(Intro-Reserved-Ultraviolet-Err)
─────────────────────────────────────────────────────────────────
Γ ⊢ Intro(x, ent) ⇑ c
```

> When multiple `Intro` rules are simultaneously applicable, a conforming implementation MUST apply the **first matching clause in the ordered priority list above**.

The crucial — and frequently surprising — rule is **`(Intro-Outer-Err)`**: a name bound in an *outer* scope **cannot be reused** as the name of a new binding in an inner scope. **Ultraviolet does not permit variable shadowing.** If a name is already visible, you must pick a different name, introduce your binding in a sibling scope where the outer name is not visible, or — when you genuinely want a second name for the *same* entity — use `using source as alias` (§5.2.3). `(Intro-Dup)` is the special case where the clash is in the very same scope (a true duplicate declaration).

```ultraviolet
procedure summarize(frame_index: u32) -> u32 {
    let base_count: u32 = frame_index * 2u32

    // OK: `scaled_count` is a fresh name not visible in any enclosing scope.
    let scaled_count: u32 = base_count + 1u32

    // ERROR (Intro-Outer-Err, E-MOD-1304): `frame_index` is already bound by
    // the parameter in the enclosing procedure scope. Shadowing is forbidden.
    // let frame_index: u32 = 0u32

    return scaled_count
}
```

#### 5.2.2 Local binding goes through `Intro`

Every local-scope introduction — pattern bindings from `let`/`var`, loop variables, `if is` and `race`/`dispatch` arm patterns — funnels through `Intro` (via the `BindPattern` driver used in the resolution pass, §5.7). Therefore the no-shadowing rule applies uniformly to locals, parameters, type parameters, and module-level names alike.

#### 5.2.3 `UsingAlias`: the sanctioned way to make a second name

When you want a *compile-time alternate name* for an entity that already resolves, use a `using ... as ...` alias. Internally this is the `UsingAlias` operation, which binds `alias_name` in the current scope to the very same `Entity` that `source_name` resolves to — it introduces no new storage and does not copy the bound entity; the alias and the source are interchangeable references to one compile-time entity.

```text
(Using-Alias-Ok)
Γ ⊢ Lookup(source_name) ⇓ ent   ¬ InScope(S_cur, alias_name)
¬ InOuter(Γ, alias_name)   ¬ ReservedId(alias_name)
Scopes(Γ') = [S_cur[IdKey(alias_name) ↦ ent]] ++ Γ_out
Project(Γ') = Project(Γ)   ResCtx(Γ') = ResCtx(Γ)
────────────────────────────────────────────────────────────
Γ ⊢ UsingAlias(source_name, alias_name) ⇓ Γ'
```

The three error rules mirror those for `Intro`, plus an unresolved-source case:

```text
(Using-Alias-Unresolved)
Γ ⊢ Lookup(source_name) ↑   c = Code(Using-Alias-Unresolved)
────────────────────────────────────────────────────────────
Γ ⊢ UsingAlias(source_name, alias_name) ⇑ c

(Using-Alias-Dup)
Γ ⊢ Lookup(source_name) ⇓ ent
(InScope(S_cur, alias_name) ∨ InOuter(Γ, alias_name))
c = Code(Using-Alias-Dup)
────────────────────────────────────────────────────────────
Γ ⊢ UsingAlias(source_name, alias_name) ⇑ c

(Using-Alias-Reserved)
ReservedId(alias_name)   c = Code(Using-Alias-Reserved)
────────────────────────────────────────────────────────────
Γ ⊢ UsingAlias(source_name, alias_name) ⇑ c
```

Again the **first matching clause** wins. Note the alias target obeys the same no-reuse rule: the alias name must be free in the current *and* all enclosing scopes. This `UsingAlias` operation backs the local statement form `using identifier as identifier` (the `using_local_stmt`), distinct from a top-level `using_decl` (§5.8).

#### 5.2.4 Module-name validation

After a module's top-level names are collected (§5.5), the whole name set is checked to ensure no collected name is a keyword:

```text
Names(N) = dom(N)

(Validate-Module-Ok)
∀ n ∈ Names(N). ¬ KeywordKey(n)
───────────────────────────────
Γ ⊢ ValidateModuleNames(N) ⇓ ok

(Validate-Module-Keyword-Err)
∃ n ∈ Names(N). KeywordKey(n)   c = Code(Validate-Module-Keyword-Err)
─────────────────────────────────────────────────────────────────────
Γ ⊢ ValidateModuleNames(N) ⇑ c
```

Reuse of a *universe* name (a primitive/special/async type) at module scope is **not** a `ValidateModuleNames` concern; it is caught by `(Intro-Outer-Err)` when the module's bindings are introduced, because `UniverseBindings` is always in the outer chain at module scope.

### 5.3 Lookup and Qualified Resolution (§7.3)

#### 5.3.1 Unqualified lookup

An unqualified name resolves by scanning the scope stack from innermost to outermost and taking the **first** scope that contains the key:

```text
Scopes(Γ) = [S_1, …, S_n]
i = min{j | IdKey(x) ∈ dom(S_j)}

(Lookup-Unqualified)
i defined
─────────────────────────
Γ ⊢ Lookup(x) ⇓ S_i[IdKey(x)]

(Lookup-Unqualified-None)
i undefined
─────────────────────────
Γ ⊢ Lookup(x) ↑
```

This innermost-first scan is what makes a parameter take precedence over a same-named module-level value — but recall from §5.2 that you cannot *create* such a clash, because `Intro` would reject it. The scan resolves names that are legitimately layered (e.g. a universe type name visible at every level versus a local value of a different kind).

#### 5.3.2 Kind-checked resolvers

`Lookup` returns an entity of any kind. The kind-specific resolvers filter by `kind`:

```text
(Resolve-Value-Name)
Γ ⊢ Lookup(x) ⇓ ent   ValueKind(ent)
──────────────────────────────────────
Γ ⊢ ResolveValueName(x) ⇓ ent

(Resolve-Type-Name)
Γ ⊢ Lookup(x) ⇓ ent   TypeKind(ent)
──────────────────────────────────────
Γ ⊢ ResolveTypeName(x) ⇓ ent

(Resolve-Class-Name)
Γ ⊢ Lookup(x) ⇓ ent   ClassKind(ent)
──────────────────────────────────────
Γ ⊢ ResolveClassName(x) ⇓ ent

(Resolve-Module-Name)
Γ ⊢ Lookup(x) ⇓ ent   ModuleKind(ent)
──────────────────────────────────────
Γ ⊢ ResolveModuleName(x) ⇓ ent
```

A name used in value position (`ResolveValueName`) that resolves to a `Type` entity does **not** satisfy `ValueKind`, so the value resolver fails — this is how the compiler distinguishes "is this a callable/value?" from "is this a type?". A specialization, `RegionAliasName`, holds when a value name resolves to a region-alias entity:

```text
RegionAliasName(Γ, x) ⇔ Γ ⊢ ResolveValueName(x) ⇓ ent ∧ RegionAlias(ent)
```

#### 5.3.3 The `::` operator and qualified resolution

The `::` operator (a member of the language's `OperatorSet`) is the **path separator**. It is used in module paths, type paths, qualified value names, and enum-literal paths. A *path* is one or more identifiers separated by `::`; a *qualified name* in expression position is a path followed by a final name:

```ebnf
qualified_name_expr ::= path "::" identifier
path                ::= identifier ("::" identifier)*
module_path         ::= identifier ("::" identifier)*
```

Qualified resolution is performed by `ResolveQualified(path, name, K)`, which (1) resolves the leading `path` to a concrete module path, (2) looks the final `name` up in that module's name map, (3) checks accessibility from the current module, and (4) confirms the result has the requested kind `K`:

```text
(Resolve-Qualified)
Γ ⊢ ResolveModulePath(path, Alias, ModuleNames) ⇓ mp
NameMap(P, mp)[IdKey(name)] = ent
Γ ⊢ CanAccess(m, DeclOf(mp, name)) ⇓ ok
K(ent)
─────────────────────────────────────────────────────
Γ ⊢ ResolveQualified(path, name, K) ⇓ ent

K ∈ {ValueKind, TypeKind, ClassKind, ModuleKind}
```

The inputs `ModulePaths`, `ModuleNames`, and `Alias` are the module-visibility data for the current module `m`:

```text
ModulePaths = VisibleModulePaths(m)
ModuleNames = VisibleModuleNames(m)
Alias       = AliasMap(m)
```

`ResolveModulePath`, `VisibleModulePaths`, `VisibleModuleNames`, and `AliasMap` are owned by §11.5.4 (covered in the **Modules & Imports** chapter); §7 consumes them but does not redefine them. The essential behavior of `ResolveModulePath` is alias-expansion followed by a direct/current-assembly fallback:

```text
(Resolve-ModulePath-Direct)
Γ ⊢ AliasExpand(path, Alias) ⇓ path'   StringOfPath(path') ∈ ModuleNames
────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveModulePath(path, Alias, ModuleNames) ⇓ path'

(Resolve-ModulePath-Current)
Γ ⊢ AliasExpand(path, Alias) ⇓ path'   StringOfPath(path') ∉ ModuleNames
path'' = CurrentAsmPath(m, path')   StringOfPath(path'') ∈ ModuleNames
────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveModulePath(path, Alias, ModuleNames) ⇓ path''
```

A leading segment that is an `import` alias is expanded first (`AliasExpand`); if the expanded path is a known module name it is used directly, otherwise it is retried as a path relative to the current assembly (`CurrentAsmPath(m, path') = [AsmOfModule(m)] ++ path'`). Failure when neither the expanded nor the current-assembly path names a module yields `(ResolveModulePath-Err)`, the unresolved-module diagnostic `E-MOD-1107`.

```ultraviolet
import Grimoire::Frame::Loop

procedure tick(loop_state: Loop::FrameLoop) -> u32 {
    // `Loop::beginFrame` is a qualified value name: the path `Loop` is an
    // import alias for `Grimoire::Frame::Loop`; `beginFrame` is the final name,
    // looked up in that module's name map and accessibility-checked.
    let frame_index: u32 = Loop::beginFrame(loop_state)
    return frame_index
}
```

### 5.4 Visibility and Accessibility (§7.4)

Qualified resolution succeeds only if the referenced declaration is **accessible** from the current module. Ultraviolet has exactly **three** visibility levels, given by the grammar:

```ebnf
visibility ::= "public" | "internal" | "private"
```

Visibility is written as an optional leading keyword on every top-level item declaration (`import_decl`, `using_decl`, `static_decl`, `procedure_decl`, `record_decl`, `enum_decl`, `modal_decl`, `class_declaration`, `type_alias_decl`, and so on). The style guide is emphatic: **always write visibility explicitly** where the language allows it; do not rely on omitted defaults; treat visibility as part of the API contract.

#### 5.4.1 The accessibility judgment

`CanAccess(m, it)` decides whether the *current* module `m` may reference declaration `it`. It depends on the item's visibility and where it lives:

```text
Vis(it) = it.vis
SameAssembly(m_1, m_2) ⇔ AsmOfModule(m_1) = AsmOfModule(m_2)

(Access-Public)
Vis(it) = `public`
─────────────────────────
Γ ⊢ CanAccess(m, it) ⇓ ok

(Access-Internal)
Vis(it) = `internal`   SameAssembly(ModuleOf(it), m)
────────────────────────────────────────────────────
Γ ⊢ CanAccess(m, it) ⇓ ok

(Access-Private)
Vis(it) = `private`   ModuleOf(it) = m
──────────────────────────────────────
Γ ⊢ CanAccess(m, it) ⇓ ok
```

Semantically, the three levels are:

| Keyword | Accessible from |
| --- | --- |
| `public` | Any module in any assembly. |
| `internal` | Any module in the **same assembly** as the declaration. |
| `private` | Only the **same module** as the declaration. |

The two failure rules both produce the access-error code `Code(Access-Err)` (`E-MOD-1207`):

```text
(Access-Internal-Err)
Vis(it) = `internal`   ¬ SameAssembly(ModuleOf(it), m)   c = Code(Access-Err)
──────────────────────────────────────────────────────────────────────────────
Γ ⊢ CanAccess(m, it) ⇑ c

(Access-Err)
Vis(it) = `private`   ModuleOf(it) ≠ m   c = Code(Access-Err)
─────────────────────────────────────────────────────────────
Γ ⊢ CanAccess(m, it) ⇑ c
```

`ModuleOf` and `DeclOf` connect a resolved name back to its declaring item:

```text
DeclOf(mp, name) = it   ⇔ ModuleOf(it) = mp ∧ it ≠ ExternBlock(…)
                            ∧ IdKey(name) ∈ dom(ItemBindings(it, mp))
DeclOf(mp, name) = proc ⇔ ExternBlockOf(proc) = blk ∧ ModuleOf(blk) = mp
                            ∧ ProcName(proc) = name
ModuleOf(it) = p ⇔ it ∈ ASTModule(P, p).items
```

Note the second clause: a foreign procedure inside an `extern` block is accessed by *its own* name, with `ModuleOf` delegated to the enclosing extern block (`ModuleOf(proc) = ModuleOf(ExternBlockOf(proc))`).

```ultraviolet
// File: Grimoire/Frame/Loop.uv  (module Grimoire::Frame::Loop)

public record FrameLoop {
    public frame_index: u32
}

// Visible across the whole Grimoire assembly, but not to other assemblies.
internal procedure advanceLoop(loop_state: FrameLoop) -> FrameLoop {
    return FrameLoop { frame_index: loop_state.frame_index + 1u32 }
}

// Visible only inside this module.
private procedure clampIndex(value: u32) -> u32 {
    if value > 240u32
        return 240u32
    return value
}
```

A module in a *different* assembly may reference `FrameLoop` (public) but not `advanceLoop` (internal); no module outside `Grimoire::Frame::Loop` may reference `clampIndex` (private).

#### 5.4.2 Top-level visibility

`CanAccess` governs *references*. A separate, trivial judgment confirms that an item being resolved is in fact a top-level declaration of its module:

```text
TopLevelDecl(it) ⇔ it ∈ ASTModule(P, ModuleOf(it)).items

(TopLevelVis-Ok)
TopLevelDecl(it)
─────────────────────────
Γ ⊢ TopLevelVis(it) ⇓ ok
```

This check is invoked once per item by the resolution driver (`ResolveItems`, §5.7).

#### 5.4.3 Visibility and `using` re-exports

When a `using` declaration is itself marked `public` (a re-export of an imported name through the current module's public surface), the **imported item must also be public**. This is enforced in §11.2.4 by the constraint `(vis = public ⇒ Vis(DeclOf(mp, item)) = public)` carried in the `using` static semantics. A `public using` of a non-public item is the error `Using-List-Public-Err` / `Using-Path-Item-Public-Err` (`E-MOD-1205`, owned by §11.2.7). See the **Modules & Imports** chapter for the full `using` static semantics; the accessibility check it performs is the §7 `CanAccess` judgment above.

### 5.5 Top-Level Name Collection (§7.5)

Before any item body is resolved, every top-level name in a module is collected into the module scope `S_module = N`. Collection is **order-independent**: the resulting name map is the same regardless of declaration order.

```text
∀ items'. Permutation(items', items) ∧ Γ ⊢ CollectNames(items, p, ∅) ⇓ N
        ⇒ Γ ⊢ CollectNames(items', p, ∅) ⇓ N
```

#### 5.5.1 What each declaration binds

`ItemBindings(it, p)` maps each top-level item to the `(name, NameInfo)` pairs it contributes, where `NameInfo = ⟨kind, origin, target_opt, source⟩` (with `BindKind = {Value, Type, Class, ModuleAlias}` and `BindSource = {Decl, Using, Import}`):

```text
(Bind-Procedure)  ProcedureDecl(…, name, …)  ↦ [(name, ⟨Value, p, ⊥, Decl⟩)]
(Bind-Record)     RecordDecl(…, name, …)     ↦ [(name, ⟨Type,  p, ⊥, Decl⟩)]
(Bind-Enum)       EnumDecl(…, name, …)       ↦ [(name, ⟨Type,  p, ⊥, Decl⟩)]
(Bind-Modal)      ModalDecl(…, name, …)      ↦ [(name, ⟨Type,  p, ⊥, Decl⟩)]
(Bind-Class)      ClassDecl(…, name, …)      ↦ [(name, ⟨Class, p, ⊥, Decl⟩)]
(Bind-TypeAlias)  TypeAliasDecl(…, name, …)  ↦ [(name, ⟨Type,  p, ⊥, Decl⟩)]
```

Two items bind *sets* of names:

```text
(Bind-ExternBlock)
B = [(name_i, ⟨Value, p, ⊥, Decl⟩) | ExternProcDecl(…, name_i, …) ∈ items]
────────────────────────────────────────────────────────────────────────
Γ ⊢ ItemBindings(ExternBlock(_, _, _, items, _, _), p) ⇓ B

(Bind-Static)
Γ ⊢ PatNames(pat) ⇓ N
────────────────────────────────────────────────────────────────────────
Γ ⊢ ItemBindings(StaticDecl(_, _, _, ⟨pat, _, _, _, _⟩, _, _), p)
    ⇓ [(n, ⟨Value, p, ⊥, Decl⟩) | n ∈ N]
```

An `extern` block binds one `Value` per foreign procedure. A `static` (top-level `let`/`var`) binds one `Value` per name in its binding pattern; `PatNames` enumerates the names a pattern introduces:

```text
PatNames(IdentifierPattern(x)) = [x]
PatNames(WildcardPattern)      = []
PatNames(LiteralPattern(lit))  = []
PatNames(TypedPattern("_", T)) = []
PatNames(TypedPattern(x, T))   = [x]   if x ≠ "_"
PatNames(TuplePattern([p_1, …, p_n]))                   = N_1 ++ … ++ N_n
PatNames(RecordPattern(_, [f_1, …, f_n]))               = N_1 ++ … ++ N_n
PatNames(EnumPattern(_, _, ⊥))                          = []
PatNames(EnumPattern(_, _, TuplePayloadPattern([…])))   = N_1 ++ … ++ N_n
PatNames(EnumPattern(_, _, RecordPayloadPattern([…])))  = N_1 ++ … ++ N_n
PatNames(RangePattern(_, p_l, p_h))                     = N_l ++ N_h
```

`import` and `using` defer to the module-system judgments:

```text
(Bind-Import)   Γ ⊢ ImportNames(u) ⇓ B  ⇒  Γ ⊢ ItemBindings(u, p) ⇓ B
(Bind-Using)    Γ ⊢ UsingNames(u)  ⇓ B  ⇒  Γ ⊢ ItemBindings(u, p) ⇓ B
```

An `import` binds one `ModuleAlias`; a `using` binds the named `Value`/`Type`/`Class` items (see §5.6, §5.8, and the Modules chapter). Both have error-propagating forms (`Bind-Import-Err`, `Bind-Using-Err`). An `ErrorItem` binds nothing (`(Bind-ErrorItem) ⇒ []`).

#### 5.5.2 Collecting and rejecting duplicates

`CollectNames` folds `ItemBindings` over the item list, accumulating into `N` and rejecting collisions:

```text
(Collect-Ok)
Γ ⊢ CollectNames(items, p, ∅) ⇓ N
─────────────────────────────────
Γ ⊢ CollectNames(M) ⇓ N

(Collect-Scan)
Γ ⊢ ItemBindings(it, p) ⇓ B   DisjointNames(B, N)   NoDup(B)
Γ ⊢ CollectNames(rest, p, N ∪ B) ⇓ N'
─────────────────────────────────────────────────────────────
Γ ⊢ CollectNames(it :: rest, p, N) ⇓ N'
```

A new item's bindings must be **disjoint** from what is already collected (`DisjointNames`) and free of internal duplicates (`NoDup`). The collision-handling helpers are:

```text
Names(B) = { n | (n, _) ∈ B }
NoDup(B) ⇔ Distinct(Names(B))
DisjointNames(B, N) ⇔ Names(B) ∩ dom(N) = ∅
UsingImportConflict(B, N) ⇔ ∃ n. n ∈ Names(B) ∩ dom(N)
    ∧ (NameSource(B, n) ∈ {Using, Import} ∨ NameSource(N, n) ∈ {Using, Import})
```

When a collision occurs, the diagnostic depends on whether a `using`/`import` name is involved:

```text
(Collect-Using-Import-Dup)
Γ ⊢ ItemBindings(it, p) ⇓ B
(¬ DisjointNames(B, N) ∨ ¬ NoDup(B))   UsingImportConflict(B, N)
c = Code(Import-Using-Name-Conflict)
─────────────────────────────────────────────────────────────────
Γ ⊢ CollectNames(it :: rest, p, N) ⇑ c

(Collect-Dup)
Γ ⊢ ItemBindings(it, p) ⇓ B
(¬ DisjointNames(B, N) ∨ ¬ NoDup(B))   ¬ UsingImportConflict(B, N)
c = Code(Collect-Dup)
─────────────────────────────────────────────────────────────────
Γ ⊢ CollectNames(it :: rest, p, N) ⇑ c
```

- A collision where one side came from `using`/`import` is **`Import-Using-Name-Conflict`** (`E-MOD-1203`).
- Any other collision (two `Decl` items with the same name) is **`Collect-Dup`** (`E-MOD-1302`, "duplicate declaration in module scope").
- A binding-error inside an item propagates as `(Collect-Err)`.

The spec also gives a small-step `NamesState` machine — `NamesStart(M) → NamesScan(items, p, N) → NamesDone(N)` (with an `Error(code)` sink) — whose steps `(Names-Step)`, `(Names-Step-Using-Import-Dup)`, `(Names-Step-Dup)`, and `(Names-Step-Err)` mirror the big-step clauses above exactly.

#### 5.5.3 Derived name maps

From the collected map `N = NameMap(P, mp)`, the spec derives convenient projections consumed across resolution:

```text
NameMap(P, mp) = N ⇔ ModuleMap(P, mp) = M ∧ Γ ⊢ CollectNames(M) ⇓ N
AliasMap(m)      = { n ↦ origin | NameMap(P, m)[n].kind = ModuleAlias }
UsingMap(m)      = { n ↦ ⟨k, origin, target_opt⟩ | source = Using ∧ k ∈ {Value, Type, Class} }
UsingValueMap(m) = { n ↦ origin | source = Using ∧ kind = Value }
UsingTypeMap(m)  = { n ↦ origin | source = Using ∧ kind ∈ {Type, Class} }
TypeMap(m)       = { n ↦ origin | kind = Type }
ClassMap(m)      = { n ↦ origin | kind = Class }
ItemNames(mp)    = { n | NameMap(P, mp)[n].kind ∈ {Value, Type, Class} }
```

`ItemNames(mp)` is exactly the set of names a `using mp::*` wildcard or a qualified `mp::name` may reach (module aliases are excluded — you cannot `using` a sub-module as if it were an item).

```ultraviolet
// Order does not matter: `helperValue` may be referenced before its
// textual declaration, because all top-level names are collected first.
procedure entry() -> u32 {
    return helperValue()
}

procedure helperValue() -> u32 {
    return 7u32
}

// ERROR (Collect-Dup, E-MOD-1302): two top-level declarations named `entry`.
// procedure entry() -> u32 { return 0u32 }
```

### 5.6 Qualified Disambiguation (§7.6)

When the parser sees `path::name` in *expression* position it does not yet know whether `name` is a qualified value (a procedure or static), a record path, or a unit enum variant. `ResolveQualifiedForm` performs that disambiguation, trying interpretations in a fixed priority order. The relevant path-shaped helpers are:

```text
(Resolve-RecordPath)
Γ ⊢ ResolveTypePath(path ++ [name]) ⇓ p   RecordDecl(p) = R
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveRecordPath(path, name) ⇓ p

(Resolve-EnumUnit)
Γ ⊢ ResolveTypePath(path) ⇓ p   EnumDecl(p) = E   VariantPayload(E, name) = ⊥
─────────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveEnumUnit(path, name) ⇓ p
```

(`ResolveEnumTuple` and `ResolveEnumRecord` are the analogous rules for variants whose payload is `TuplePayload(_)` or `RecordPayload(_)`; the full judgment set is `ResolvePathJudg = {ResolveRecordPath, ResolveEnumUnit, ResolveEnumTuple, ResolveEnumRecord}`.)

`ResolveQualifiedForm(QualifiedName(path, name))` then dispatches:

```text
(ResolveQual-Name-Builtin)
BuiltinValuePath(path, name)
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇓ Path(path, name)

(ResolveQual-Name-Value)
Γ ⊢ ResolveQualified(path, name, ValueKind) ⇓ ent
ent.origin_opt = mp   name' = (ent.target_opt if present, else name)
PathOfModule(mp) = path'
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇓ Path(path', name')

(ResolveQual-Name-Record)
Γ ⊢ ResolveRecordPath(path, name) ⇓ p   SplitLast(p) = (mp, name')
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇓ Path(mp, name')

(ResolveQual-Name-Enum)
Γ ⊢ ResolveQualified(path, name, ValueKind) ↑
Γ ⊢ ResolveRecordPath(path, name) ↑
Γ ⊢ ResolveEnumUnit(path, name) ⇓ p
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇓ EnumLiteral(FullPath(p, name), ⊥)

(ResolveQual-Name-Err)
Γ ⊢ ResolveQualified(path, name, ValueKind) ↑
Γ ⊢ ResolveRecordPath(path, name) ↑
Γ ⊢ ResolveEnumUnit(path, name) ↑   c = Code(ResolveExpr-Ident-Err)
─────────────────────────────────────────────────────────────
Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇑ c
```

The disambiguation order is: **builtin static value → qualified value → record path → unit enum literal → error**. (`BuiltinValuePath(path, name) ⇔ BuiltinModalStaticSig(path, name)` is defined, i.e. the path/name names a built-in modal static.) The canonicalization to `Path(path', name')` rewrites import aliases and `using ... as ...` targets back to their *resolved* module path and original name (via `ent.target_opt` and `PathOfModule`), so the post-resolution AST always carries the fully-qualified, canonical reference. A failure to interpret the form any of these ways is the unresolved-name error `ResolveExpr-Ident-Err` (`E-MOD-1301`).

The grammar for the three expression shapes this drives is:

```ebnf
qualified_name_expr ::= path "::" identifier
path_expr           ::= type_path "::" identifier
enum_literal        ::= type_path "::" identifier variant_args?
variant_args        ::= "(" expression_list ")" | "{" field_init_list "}"
```

```ultraviolet
import Grimoire::Frame::State

procedure reportState() -> State::PlaybackState {
    // `State::PlaybackState::Playing` is an `enum_literal`: the type path
    // `State::PlaybackState` resolves to an enum, and `Playing` is a unit
    // variant of it, so (ResolveQual-Name-Enum) yields an enum literal.
    return State::PlaybackState::Playing
}
```

### 5.7 Shared Resolution Helpers and the Resolution Pass (§7.7)

The resolution pass walks every module's AST and rewrites each name reference into its canonical resolved form. `Γ`'s project/module data for the pass is fixed as:

```text
P = Project(Γ)   m = CurrentModule(Γ)   M = ASTModule(P, m)
ResolveInputs  = ⟨M, ModulePaths, { NameMap(P, p) | p ∈ ModulePaths }⟩
ResolveOutputs = ⟨M'⟩
```

#### 5.7.1 Scope push and type-parameter bindings

A new local scope is pushed by prepending an empty map:

```text
PushScope(Γ) = Γ' ⇔ Scopes(Γ') = [∅] ++ Scopes(Γ)
                  ∧ Project(Γ') = Project(Γ) ∧ ResCtx(Γ') = ResCtx(Γ)
```

Generic type parameters are introduced as `Type` entities with no origin:

```text
TypeParamBindings(params) = { IdKey(p.name) ↦ ⟨Type, ⊥, ⊥, Decl⟩ | p ∈ params }
TypeParamBindings(⊥) = {}
```

Constructs that bind a pattern — block, loop, `if is`, `if`-case, `dispatch`, `race` arm — each `PushScope` first, then `BindPattern` the (already resolved) pattern into the fresh scope, then resolve the body in the extended context. For example:

```text
(ResolveExpr-Block)
Γ_0 = PushScope(Γ)   Γ_0 ⊢ ResolveStmtSeq(stmts) ⇓ (Γ_1, stmts')
(tail_opt = ⊥ ⇒ tail_opt' = ⊥)
(tail_opt = e ⇒ Γ_1 ⊢ ResolveExpr(e) ⇓ e' ∧ tail_opt' = e')
─────────────────────────────────────────────────────────────────
Γ ⊢ ResolveExpr(BlockExpr(stmts, tail_opt)) ⇓ BlockExpr(stmts', tail_opt')

(ResolveExpr-LoopIter)
Γ ⊢ ResolvePattern(pat) ⇓ pat'   …   Γ_0 = PushScope(Γ)
Γ_0 ⊢ BindPattern(pat') ⇓ Γ_1   Γ_1 ⊢ ResolveExpr(body) ⇓ body'
─────────────────────────────────────────────────────────────────
Γ ⊢ ResolveExpr(LoopIter(pat, ty_opt, iter, inv_opt, body)) ⇓ LoopIter(…)
```

Because each binding pushes a new scope and `BindPattern` routes through `Intro`, the no-shadowing rule (§5.2) holds across all these nested constructs. Note that in `(ResolveExpr-LoopIter)`, `(ResolveExpr-IfIs)`, and the `if`-case/`dispatch`/`race`-arm rules the *pattern itself* is resolved in the **outer** `Γ` (so any type paths in the pattern resolve against the surrounding scope), while the names it binds are introduced into the freshly pushed scope.

#### 5.7.2 Resolving type, class, and value paths

Type and class paths resolve through the same machinery as qualified names, but filtered by kind. A single-segment path resolves locally; a multi-segment path resolves through `ResolveQualified`:

```text
(ResolveTypePath-Ident)
|path| = 1   Γ ⊢ ResolveTypeName(path[0]) ⇓ ent   ent.origin_opt = p
name = (ent.target_opt if present, else path[0])
──────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveTypePath(path) ⇓ FullPath(PathOfModule(p), name)

(ResolveTypePath-Ident-Local)
|path| = 1   Γ ⊢ ResolveTypeName(path[0]) ⇓ ent   ent.origin_opt = ⊥
name = (ent.target_opt if present, else path[0])
──────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveTypePath(path) ⇓ [name]

(ResolveTypePath-Qual)
|path| ≥ 2   path = p ++ [name]
Γ ⊢ ResolveQualified(p, name, TypeKind) ⇓ ent   ent.origin_opt = mp
name' = (ent.target_opt if present, else name)
──────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveTypePath(path) ⇓ FullPath(PathOfModule(mp), name')
```

`ResolveClassPath` is the exact analogue with `ResolveClassName`/`ClassKind` (`ResolveClassPath-Ident` and `ResolveClassPath-Qual`). A single-segment local type parameter (origin `⊥`) is left as a bare `[name]` via `(ResolveTypePath-Ident-Local)` — characterized by `LocalTypePath(path)`; everything else is canonicalized to a `FullPath` rooted at its declaring module (`FullPath(path, name) = path ++ [name]`). `ResolveType` then recurses structurally over `TypePath` (`ResolveType-Path`), `TypeDynamic` (resolving a class path), `TypeApply`, `TypeModalState` (resolving the modal ref via `ResolveModalRef`), and all other homomorphic constructors (`ResolveType-Hom`).

#### 5.7.3 Resolving expressions

`ResolveExpr` rewrites identifiers and qualified forms while leaving structure intact:

```text
(ResolveExpr-Ident)
Γ ⊢ ResolveValueName(x) ⇓ ent
──────────────────────────────────────
Γ ⊢ ResolveExpr(Identifier(x)) ⇓ Identifier(x)

(ResolveExpr-Ident-Err)
Γ ⊢ ResolveValueName(x) ⇑   c = Code(ResolveExpr-Ident-Err)
───────────────────────────────────────────────────────────
Γ ⊢ ResolveExpr(Identifier(x)) ⇑ c

(ResolveExpr-Qualified)
Γ ⊢ ResolveQualifiedForm(e) ⇓ e'
──────────────────────────────────
Γ ⊢ ResolveExpr(e) ⇓ e'
```

A bare identifier that does not resolve as a value is the unresolved-name error `E-MOD-1301`. Call resolution is kind-aware (`ResolveCallee`): a callee identifier may resolve as a value (`ResolveCallee-Ident-Value`), or — when there are no arguments and it names a record type — as a record constructor (`ResolveCallee-Ident-Record`); a qualified callee may resolve as a value, a builtin, or a record path (`ResolveCallee-Path-Value`, `-Path-Builtin`, `-Path-Record`). The full set of expression rules (`ResolveExprRules`) covers calls (`ResolveExpr-Call`, `-Call-TypeArgs`), record/enum literals, control-flow (`if is`, `if`-case, loops, blocks), parallel/spawn/dispatch/race/all, yield, sync, and allocation forms; the catch-all `ResolveExpr-Hom` recurses through any constructor for which no specific rule's premises hold:

```text
NoSpecificResolveExpr(C(e_1, …, e_n)) ⇔
  ¬ ∃ r ∈ ResolveExprRules \ {ResolveExpr-Hom}. PremisesHold(r, e)

(ResolveExpr-Hom)
NoSpecificResolveExpr(C(e_1, …, e_n))   ∀ i, Γ ⊢ ResolveExpr(e_i) ⇓ e_i'
─────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveExpr(C(e_1, …, e_n)) ⇓ C(e_1', …, e_n')
```

Region allocation has two source forms. `new value` is contextual syntax for
the current scoped region and does not resolve `new` as a value name. A named
region handle introduced by `region as r` is referenced as a value and targeted
with the ordinary method-call form `r~>alloc(value)`; `^` is only the bitwise
XOR operator.

#### 5.7.4 Module path validation and the module driver

Before a module's items are resolved, its own path is validated against the reserved-path rule (§5.1.5):

```text
(Validate-ModulePath-Ok)
¬ ReservedModulePath(PathOfModule(p))
─────────────────────────────────────
Γ ⊢ ValidateModulePath(p) ⇓ ok

(Validate-ModulePath-Reserved-Err)
ReservedModulePath(PathOfModule(p))   c = Code(Validate-ModulePath-Reserved-Err)
─────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ValidateModulePath(p) ⇑ c
```

The module driver ties it all together: collect names, validate the path, validate the names against keywords, set `S_module = N`, build the resolution stack `Γ_N = [S_module, S_universe]`, then resolve every item:

```text
(ResolveModule-Ok)
Γ ⊢ CollectNames(M) ⇓ N   Γ ⊢ ValidateModulePath(M.path) ⇓ ok
Γ ⊢ ValidateModuleNames(N) ⇓ ok   S_module = N
Γ_N = [S_module, S_universe]   Γ_N ⊢ ResolveItems(M.items) ⇓ items'
────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveModule(M) ⇓ ⟨M.path, items', M.module_doc⟩

(ResolveItems-Cons)
Γ ⊢ TopLevelVis(it) ⇓ ok   Γ ⊢ ResolveItem(it) ⇓ it'   Γ ⊢ ResolveItems(rest) ⇓ rest'
─────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveItems(it :: rest) ⇓ it' :: rest'
```

Note that when item resolution begins, the scope stack is **exactly** `[S_module, S_universe]` — there are no local or procedure scopes yet; those are pushed as bodies are entered. `ResolveItem` itself is *feature-owned*: chapters 11 through 22 supply the feature-specific `ResolveItem` clauses (records, enums, procedures, etc.); §7 owns only this shared driver and the helpers above. The whole-project pass is `ResolveModules`:

```text
(ResolveModules-Ok)
Γ ⊢ ParseModules(P) ⇓ [M_1, …, M_k]   ∀ i, Γ ⊢ ResolveModule(M_i) ⇓ M_i'
─────────────────────────────────────────────────────────────────────────
Γ ⊢ ResolveModules(P) ⇓ [M_1', …, M_k']
```

with `(ResolveModules-Err-Parse)` and `(ResolveModules-Err-Resolve)` propagating the first parse or resolution failure respectively. The state-machine form (`ResStart → ResNames → ResItems → ResDone`) mirrors the big-step rules.

### 5.8 Referencing Names Across Modules and Assemblies

Putting §5.3, §5.4, and §11.5.4 together, there are exactly two ways to name a declaration that lives in another module, and one prerequisite for both.

**Prerequisite — assembly visibility.** A foreign module's names are reachable only if the foreign module's assembly is *visible* from the current module. The current assembly is always visible; a foreign assembly becomes visible by `import`ing one of its modules:

```text
VisibleAssemblies(m)  = {AsmOfModule(m)} ∪ {AsmOfPath(p) | p ∈ ImportPaths(m)}
VisibleModulePaths(m) = { p | p ∈ AllModulePaths(P) ∧ AsmOfPath(p) ∈ VisibleAssemblies(m) }
```

Cross-assembly qualified use additionally requires that an `import` *covers* the target module path (`ImportRequired(m, path) ⇔ AsmOfPath(path) ≠ AsmOfModule(m)`, and `ImportCovers` must hold); an uncovered cross-assembly reference is `Import-Using-Missing` (`E-MOD-1201`, owned by §11.5.7). Within the same assembly no `import` is needed for visibility (`Import-Ok-Local`).

**1. Qualified reference (`path::name`).** Import the module (giving a `ModuleAlias`, optionally renamed with `as`), then write `Alias::name`. This is `ResolveQualified` (§5.3.3) followed by the `CanAccess` check (§5.4).

**2. Direct reference via `using`.** A `using` declaration brings a foreign item's name into the current module scope so you can use it *unqualified*. The three forms are:

```ebnf
using_clause    ::= module_path "::" identifier ("as" identifier)?
                  | module_path "::" using_list
                  | module_path "::" "*"
using_list      ::= "{" using_specifier ("," using_specifier)* ","? "}"
using_specifier ::= identifier ("as" identifier)?
```

Each `using` binding is accessibility-checked at collection time (`CanAccess(m, DeclOf(mp, item)) ⇓ ok`) and conflict-checked against existing names (`E-MOD-1203`). The wildcard form `using mp::*` binds every accessible `Value`/`Type`/`Class` of `mp`; in a module that exposes public API (`PublicAPI(m)`) it additionally emits the wildcard warning `W-MOD-1201`. A `public using` requires the re-exported item itself be `public` (else `E-MOD-1205`), a duplicate item in a list is `E-MOD-1206`, and a `using` path that resolves to no item is `E-MOD-1204`. Full `using` static semantics are in the **Modules & Imports** chapter; the accessibility and conflict rules it invokes are precisely the §7 rules of this chapter.

```ultraviolet
// Cross-assembly: import makes the foreign assembly visible, then qualify.
import Vellum::Assets::Manifest

procedure loadManifest(path: string) -> Manifest::AssetManifest {
    return Manifest::buildManifest(path)
}
```

```ultraviolet
// Same-assembly direct reference: `using` brings the names in unqualified,
// with `FrameLoop` aliased to `Loop`.
using Grimoire::Frame::State::{ PlaybackState, FrameLoop as Loop }

procedure currentState(loop_handle: Loop) -> PlaybackState {
    return PlaybackState::Playing
}
```

### 5.9 Idioms & Best Practices

- **Always write visibility explicitly.** Per the style guide, treat `public` / `internal` / `private` as part of the API contract on every top-level item; never lean on an omitted default in project code.
- **Default to the narrowest visibility that works.** Use `private` for module-internal helpers, `internal` for assembly-wide collaborators, and reserve `public` for the genuine API surface. Keep public module roots stable.
- **Never simulate shadowing with name churn.** Because `(Intro-Outer-Err)` forbids shadowing outright, do not rename variables to fake ownership or state changes. The style guide is explicit: "Do not use name churn to simulate shadowing or ownership changes." Model lifecycle/state changes with `modal` types (see the **Modal Types** chapter), not with re-bound names.
- **Alias only when it earns its keep.** Use `using ... as ...` (the `UsingAlias` mechanism) only when the alias meaningfully improves clarity or resolves a real collision. An alias references the *same* compile-time entity — it is not a copy.
- **Order imports foundational → specific, aliases last.** Foundational/built-in imports first, engine/project imports next, aliases last; a `using module::*` (only ever in internal or implementation modules) goes after the regular imports and regular `using` declarations.
- **Never wildcard-`using` in a public API module.** `using mp::*` is allowed only in internal or implementation modules; in a module with public API it triggers `W-MOD-1201`. Prefer exact-name `using` lists or qualified references in public-facing code.
- **Prefer qualified references for clarity at boundaries.** A qualified `Module::name` reads as self-documenting provenance; reach for unqualified `using` imports inside a module only when the names are used densely.
- **Rely on order-independent collection deliberately.** Because top-level names are collected before bodies resolve, you may reference any module-level declaration regardless of textual order; organize files by responsibility and stable-to-local member ordering rather than by definition-before-use.

### 5.10 Pitfalls & Diagnostics

The following diagnostics are owned by §7.8. Each maps to the rule(s) above.

| Code | Meaning | Triggering rule(s) |
| --- | --- | --- |
| `E-CNF-0406` | A user declaration uses the reserved `gen_` prefix. | `Intro-Reserved-Gen-Err` |
| `E-MOD-1203` | A name introduced by `using` or `import as` conflicts with an existing name. | `Import-Using-Name-Conflict` |
| `E-MOD-1207` | Cannot access a non-`public`/out-of-scope item from this scope. | `Access-Err` |
| `E-MOD-1301` | Unresolved name: identifier not found in any accessible scope. | `ResolveExpr-Ident-Err` |
| `E-MOD-1302` | Duplicate declaration in module scope. | `Collect-Dup`, `Intro-Dup`, `Names-Step-Dup` |
| `E-MOD-1304` | Name reuse: identifier already bound in an enclosing scope (no shadowing). | `Intro-Outer-Err` |
| `E-MOD-1307` | Ambiguous name or method resolution; disambiguation required. | — |
| `E-MOD-1308` | `using source as alias`: `source` does not resolve in any accessible scope. | `Using-Alias-Unresolved` |
| `E-MOD-1309` | `using source as alias`: `alias` conflicts with an existing binding here or in an enclosing scope. | `Using-Alias-Dup` |
| `E-MOD-1310` | `using source as alias`: `alias` is a reserved identifier. | `Using-Alias-Reserved` |

Common mistakes and how they surface:

- **Trying to shadow → `E-MOD-1304`.** Re-binding a name that is already visible in any enclosing scope (a parameter inside the body, a module-level name inside a procedure, a universe type name anywhere) fails. When the outer binding is a *universe* name (a primitive, special, or async type — e.g. naming a local `string` or `bool`, or declaring a class `Clone`), the message SHOULD identify the category. Fix: choose a fresh name, or use `using ... as ...` for a deliberate alias of the same entity.
- **Reusing `Drop`, `Bitcopy`, `Clone`, `FfiSafe`, or `GpuSafe`** as a class or value binding fails via `(Intro-Outer-Err)` (`E-MOD-1304`) — these are reserved foundational class names in `SpecialTypeNames` and live in the universe scope.
- **`gen_`-prefixed identifiers → `E-CNF-0406`,** and the module-path validator rejects reserved module paths (anything beginning with `ultraviolet` or containing a `gen_` segment) via `Validate-ModulePath-Reserved-Err`. Using a keyword as a module-scope name is `Validate-Module-Keyword-Err`.
- **Two declarations sharing a name → `E-MOD-1302`;** if either side is a `using`/`import` name, you instead get `E-MOD-1203`. The collision is detected at collection time and is order-independent, so reordering declarations will not hide it.
- **Referencing a `private` item from another module, or an `internal` item from another assembly → `E-MOD-1207`.** Widen the declaration's visibility deliberately, or move the caller into the same module/assembly — do not work around accessibility.
- **`public using` of a non-public item → `E-MOD-1205`** (owned by §11.2.7): you cannot re-export through your public surface something the source module did not itself make `public`.
- **An unresolved bare or qualified name → `E-MOD-1301`.** For qualified forms this fires only after the value, record-path, and unit-enum-literal interpretations have all failed (`ResolveQual-Name-Err`). Check that the module is imported (cross-assembly), that the leading path/alias resolves, and that the final name is spelled and cased exactly (recall NFC normalization makes `IdKey` the true identity). An unresolved *module path* prefix instead surfaces as `E-MOD-1107` (`ResolveModulePath-Err`).

See also: **Modules & Imports** (the `import`/`using` static semantics and assembly/coverage model consumed here), **Types** (type-path resolution), **Modal Types** (modeling state without name churn), and the project's naming matrix in the style guide for the casing each identifier category must use.
