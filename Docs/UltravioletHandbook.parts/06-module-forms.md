## 6. Module-Level Forms: Imports, Using, Statics & Extern Shell

A module-level form is a *top-level item* — an entry in `top_level_item` that may appear directly inside a `.uv` source file, outside any procedure, type, or block. This chapter covers the five module-level forms that wire a module to the rest of a project and establish its module-scope state and foreign-function shell:

- **`import` declarations** — make another module's *assembly* visible so its items can be named (§11.1).
- **`using` declarations** — pull individual items (or every item) of a module into the current module's namespace (§11.2).
- **`static` declarations** — module-scope `let`/`var` bindings: constants and statics (§11.3).
- **The `extern` block shell** — the outer ABI-bearing container for foreign procedures (§11.4; full FFI detail in the FFI chapter).
- **Module and file aggregation** — how a directory of `.uv` files becomes one module, and how module paths and import coverage resolve (§11.5).

For reference, the full set of top-level items is the Appendix B production:

```ebnf
top_level_item ::= import_decl | using_decl | static_decl | procedure_decl
                 | comptime_procedure_decl | record_decl | enum_decl | modal_decl
                 | class_declaration | type_alias_decl | extern_block | derive_target_decl
```

Every one of these forms begins with an optional `attribute_list?` followed by an optional `visibility?`. Visibility is `visibility ::= "public" | "internal" | "private"`; when the `visibility?` is omitted, the parser supplies the default per rule **(Parse-Vis-Default)**:

```text
¬ IsKw(Tok(P), v)
──────────────────────────────────────
Γ ⊢ ParseVis(P) ⇓ (P, `internal`)
```

So the implicit visibility of any module-level form is **`internal`**, not `private` and not `public`. The style guide nevertheless requires you to write visibility explicitly on project code (see *Idioms & Best Practices*). Accessibility itself — what `public`/`internal`/`private` *mean* when an item is referenced from elsewhere — is owned by the Visibility & Accessibility chapter (Chapter 7); this chapter references it only where a form's well-formedness depends on it.

---

### 6.1 Import Declarations (§11.1)

An `import` declaration names another **module path** and makes the assembly that owns that path *visible* to the current module. Imports are the gate that cross-assembly `using` declarations and cross-assembly qualified references must pass through: without an import that covers a foreign module, you cannot name its items.

#### Syntax

```ebnf
import_decl ::= attribute_list? visibility? "import" module_path ("as" identifier)?
module_path ::= identifier ("::" identifier)*
```

`module_path` is the canonical path form: identifiers separated by the `::` path operator (defined in §11.5.1). The optional `"as" identifier` introduces an **alias** for the imported module path.

#### Semantics

An `import` is **compile-time only**: it introduces no runtime action, no storage, and no lowering (§11.1.5–§11.1.6). Its entire effect is name binding and assembly visibility.

Resolution runs `ResolveImportPath(path)` (§11.5.4). The path string is first looked up directly against every module name in the project (`Resolve-Import-Direct`); if that fails, it is retried with the current assembly prefixed via `CurrentAsmPath` (`Resolve-Import-Current`); if both fail, the import is unresolved (`Resolve-Import-Err`). The resulting module path `mp` is bound under a name by rule **(Import-Path)**:

```text
u = ⟨ImportDecl, vis, path, alias_opt, _, _⟩
Γ ⊢ ResolveImportPath(path) ⇓ mp
name = alias_opt if present, else Last(path)
──────────────────────────────────────────────────────────
Γ ⊢ ImportNames(u) ⇓ [(name, ⟨ModuleAlias, mp, ⊥, Import⟩)]
```

Two consequences follow directly from this rule:

1. **The bound name is a module alias, not an item.** An `import` binds a `ModuleAlias`. You still qualify items through it (`Loop::buildFrame`) or pull them in with `using`. An `import` alone does not bring any procedure, type, or constant into scope as a bare name.
2. **The default bound name is the last path segment.** `import Grimoire::Frame::Loop` binds the name `Loop` as an alias to that module path. `import Grimoire::Frame::Loop as FrameLoop` binds `FrameLoop` instead.

Beyond binding, an `import` extends the set of **visible assemblies** for the module. `VisibleAssemblies(m)` is the current module's own assembly plus the assembly of every successfully resolved import path (§11.5.4). This is what makes cross-assembly `using` and qualified references legal — see §6.5 on **import coverage**.

#### Diagnostics

| Code | Severity | Condition |
| --- | --- | --- |
| `E-MOD-1202` | Error | Import of a non-existent assembly or module (`Resolve-Import-Err`). |

Import-*coverage* violations (a `using` or reference whose path is not covered by any import) are owned by §11.5.7 (`E-MOD-1201`), not by this section. Accessibility violations are owned by Chapter 7.

#### Worked Example

```ultraviolet
import Grimoire::Frame::Loop
import Grimoire::Inkwell::FrameGraph as RenderGraph

public procedure runOnce(request: Loop::FrameRequest) -> RenderGraph::FrameReply {
    let reply: RenderGraph::FrameReply = Loop::step(request)
    return reply
}
```

Here `Loop` is the default alias for `Grimoire::Frame::Loop`, and `RenderGraph` is an explicit alias for `Grimoire::Inkwell::FrameGraph`. Both modules' items are reachable by qualifying through the alias (`Loop::FrameRequest`, `RenderGraph::FrameReply`, `Loop::step`). The aliasing here is genuine — it disambiguates a long path into a domain-meaningful name and is the recommended way to alias (the style guide forbids using name churn to simulate shadowing or ownership changes; alias only with `import … as …` / `using … as …` where aliasing is genuinely needed).

---

### 6.2 Using Declarations (§11.2)

A `using` declaration brings the **items** of a module — values, types, or classes — into the current module's namespace so they can be referenced by their bare names instead of a qualified path. There are three clause forms: a single item (optionally renamed), a brace-enclosed list, and a wildcard.

#### Syntax

```ebnf
using_decl      ::= attribute_list? visibility? "using" using_clause
using_clause    ::= module_path "::" identifier ("as" identifier)?
                  | module_path "::" using_list
                  | module_path "::" "*"
using_list      ::= "{" using_specifier ("," using_specifier)* ","? "}"
using_specifier ::= identifier ("as" identifier)?
```

The `using_list` form above is the canonical Appendix B production: it permits an **optional trailing comma** before the closing brace (`","? "}"`), and the parser accepts it via rule **(Parse-UsingListTail-TrailingComma)**. (The §11.2.1 restatement of `using_list` omits the `","?`; Appendix B and the parsing rules are authoritative, so the trailing comma is legal.)

Like `import`, a `using` declaration is **compile-time only** (§11.2.5–§11.2.6): it produces name bindings and nothing else at runtime.

> **Distinguish the local statement.** Inside a procedure body you may also write the *statement* form `using_local_stmt ::= "using" identifier "as" identifier terminator` (§18.3 / Appendix B.5). That is a block-level alias statement, not a top-level form; it requires an `as` clause and takes a bare identifier (not a `module_path`). Do not confuse it with the module-level `using` declarations described here, which are covered with statements in the Statements chapter.

#### 6.2.1 Single-item using

```ebnf
using_clause ::= module_path "::" identifier ("as" identifier)?
```

The path `module_path :: identifier` must resolve to a single item via `ResolveUsingPath` (which splits the last segment as the item name and resolves the prefix as a module through `ItemOfPath`). The item's kind `k` must be `Value`, `Type`, or `Class`. The item must be accessible (`CanAccess`), and the import that covers its module must be present (`ImportOk`). The bound name is the alias if given, else the item's own name. Rule **(Using-Item)**:

```text
u = ⟨UsingDecl, vis, ⟨UsingItem, mp_raw, item, alias_opt⟩, _, _⟩
Γ ⊢ ResolveUsingPath(mp_raw ++ [item]) ⇓ ⟨mp, item⟩
Γ ⊢ ImportOk(m, mp) ⇓ ok
Γ ⊢ CanAccess(m, DeclOf(mp, item)) ⇓ ok
(vis = `public` ⇒ Vis(DeclOf(mp, item)) = `public`)
NameMap(P, mp)[IdKey(item)] = ⟨k, _, _, _⟩    k ∈ {Value, Type, Class}
name = alias_opt if present, else item
──────────────────────────────────────────────────────────────
Γ ⊢ UsingNames(u) ⇓ [(name, ⟨k, mp, item, Using⟩)]
```

The **public re-export rule** is the parenthesized premise: a `public using` may only re-export an item that is itself `public`. Re-exporting a non-public item under `public` is `E-MOD-1205` (`Using-Path-Item-Public-Err`).

```ultraviolet
import Grimoire::Behavior::Compiler

using Grimoire::Behavior::Compiler::SessionContext
using Grimoire::Behavior::Compiler::buildPackage as compilePackage

public procedure boot(context: SessionContext) -> SessionContext {
    return compilePackage(context)
}
```

Here `SessionContext` (a type) and `compilePackage` (an aliased value — the procedure `buildPackage`) are pulled in by bare name. `compilePackage(context)` is an ordinary value call; the type annotation `SessionContext` is the in-scope bare type name.

#### 6.2.2 Using list

```ebnf
using_clause ::= module_path "::" using_list
using_list   ::= "{" using_specifier ("," using_specifier)* ","? "}"
```

A list shares one module prefix and pulls several items in one declaration. Each `using_specifier` is an item name with an optional `as` alias. The **bound names** (after applying aliases) must be **distinct** — duplicates are `E-MOD-1206`. The module prefix must resolve and be import-covered (`ImportOk`); each named item must be accessible and of kind `Value`/`Type`/`Class`; and under `public` visibility every listed item must itself be `public`. Rule **(Using-List)**:

```text
u = ⟨UsingDecl, vis, ⟨UsingList, mp_raw, specs⟩, _, _⟩
Distinct(UsingSpecNames(specs))
Γ ⊢ ResolveImportPath(mp_raw) ⇓ mp    Γ ⊢ ImportOk(m, mp) ⇓ ok
∀ i, s_i = ⟨name_i, alias_i⟩    NameMap(P, mp)[IdKey(name_i)] = ⟨k_i, _, _, _⟩    k_i ∈ {Value, Type, Class}
Γ ⊢ CanAccess(m, DeclOf(mp, name_i)) ⇓ ok
(vis = `public` ⇒ Vis(DeclOf(mp, name_i)) = `public`)
bind_i = ⟨UsingSpecName(s_i), ⟨k_i, mp, name_i, Using⟩⟩
──────────────────────────────────────────────────────────────
Γ ⊢ UsingNames(u) ⇓ [bind_1, …, bind_n]
```

`UsingSpecName(⟨name, alias_opt⟩)` is `alias_opt` when an alias is present (`alias_opt ≠ ⊥`), otherwise `name`; distinctness is checked against these resolved names, so two items renamed to the same alias collide just as two unaliased duplicates would (`Using-List-Dup` → `E-MOD-1206`).

```ultraviolet
import Grimoire::Inkwell::FrameGraph

using Grimoire::Inkwell::FrameGraph::{
    FrameRequest,
    FrameReply as RenderReply,
    submitGraph,
}

internal procedure submit(request: FrameRequest) -> RenderReply {
    return submitGraph(request)
}
```

`FrameRequest` (a type) and `submitGraph` (a value) are bound under their own names; `FrameReply` is re-bound as `RenderReply`. The trailing comma after `submitGraph` is permitted by the Appendix B `using_list` production.

#### 6.2.3 Using wildcard (`module::*`)

```ebnf
using_clause ::= module_path "::" "*"
```

A wildcard `using module::*` binds **every accessible item** of the resolved module into the current namespace. The bound set is exactly those item names `name ∈ ItemNames(mp)` that `CanAccess` admits; each is bound under its own name with kind `Value`/`Type`/`Class`. Under `public` visibility, every wildcard-bound item must be `public`.

The wildcard has **two rule variants that differ only in whether a warning is emitted**, keyed on whether the *current* module exposes a public API. `PublicAPI(m)` holds iff the module declares at least one `public` item (`PublicAPI(m) ⇔ ∃ it ∈ ASTModule(P, m).items. Vis(it) = ``public```):

```text
(Using-Wildcard-Warn)  — when PublicAPI(m) holds:  bind every accessible item AND Emit(W-MOD-1201)
(Using-Wildcard)       — when ¬PublicAPI(m):        bind every accessible item, no warning
```

So a wildcard `using` is legal anywhere it resolves and is import-covered, but it raises **`W-MOD-1201`** when used inside a module that has a public API surface. This is the spec-level backing for the style rule: wildcard `using` belongs only in internal/implementation modules, never in public API modules.

```ultraviolet
// Allowed without a warning: this implementation module declares no `public`
// item, so PublicAPI(m) is false and the wildcard does not trigger W-MOD-1201.
import Grimoire::Frame::Loop

using Grimoire::Frame::Loop::*

internal procedure tick(request: FrameRequest) -> FrameReply {
    return step(request)
}
```

`FrameRequest`, `FrameReply`, and `step` are all reached by bare name because the wildcard bound every accessible item of `Grimoire::Frame::Loop`.

#### Diagnostics

| Code | Severity | Condition |
| --- | --- | --- |
| `E-MOD-1204` | Error | Using path does not resolve to an item (`Resolve-Using-None`). |
| `E-MOD-1205` | Error | `public using` of a non-public item (`Using-List-Public-Err`, `Using-Path-Item-Public-Err`). |
| `E-MOD-1206` | Error | Duplicate bound name in a `using` list (`Using-List-Dup`). |
| `W-MOD-1201` | Warning | Wildcard `using` in a module that exposes a public API (`Using-Wildcard-Warn`). |
| `E-MOD-1201` | Error | A `using` path crosses assemblies without a covering `import` (`Import-Using-Missing`, owned by §11.5.7). |

---

### 6.3 Static Declarations (§11.3)

A **static declaration** is a top-level `let` or `var` binding: module-scope storage that lives for the lifetime of the module. These are the project's constants (`let`) and module-scope statics (`var`).

#### Syntax

```ebnf
static_decl  ::= attribute_list? visibility? ("let" | "var") binding_decl
binding_decl ::= pattern (":" type)? binding_op expression
binding_op   ::= "=" | ":="
```

The leading keyword sets `mut ∈ {let, var}`:

- `let` — an **immutable** module-scope binding (a constant or fixed static value).
- `var` — a **mutable** module-scope binding (a runtime static).

The `binding_op` is either `=` (move-on-init: `MovOf("=") = mov`) or `:=` (no-move init: `MovOf(":=") = immov`), exactly as for local bindings; the distinction governs whether the initializer is moved into the static via `StaticBindInfo`.

#### Semantics and mandatory rules

Three well-formedness rules govern every static declaration.

**1. The type annotation is mandatory.** At module scope the `(":" type)?` is *not* optional in practice: omitting it is an error. Rule **(WF-StaticDecl-MissingType)** fires `E-TYP-1505` when `ty_opt = ⊥`:

```text
item = StaticDecl(_, _, _, ⟨pat, ty_opt, op, init, _⟩, _, _)    ty_opt = ⊥
──────────────────────────────────────────────────────────────────────────
Γ ⊢ item ⇑ c        (E-TYP-1505)
```

Module-scope bindings do not get bidirectional inference from surrounding context the way locals can; you must write the type. The annotated type `T_a` then *checks* the initializer (`Γ; ⊥; ⊥ ⊢ init ⇐ T_a`) and the pattern (`Γ ⊢ pat ⇐ T_a ⊣ B`), and the pattern's bound names must be distinct (`Distinct(PatNames(pat))`, rule **(WF-StaticDecl)**).

**2. The annotation must match the initializer.** If the inferred initializer type `θ(T_i)` is not a subtype of the annotation `T_a`, rule **(WF-StaticDecl-Ann-Mismatch)** fires `E-MOD-2402`.

**3. `public var` is forbidden.** Public mutable module-scope state is illegal. The predicate is:

```text
StaticVisOk(vis, mut) ⇔ ¬ (vis = `public` ∧ mut = `var`)
```

A `public var` violates `StaticVisOk` and fires rule **(StaticVisOk-Err)** → `E-MOD-2433`. A `var` may be `internal` or `private`; only the `public` + `var` combination is rejected. (`let` is unrestricted across all three visibilities.)

#### Initialization, ordering, and naming

A `StaticDecl` introduces module-scope storage; initialization and destruction follow the module init/deinit framework (Chapter 24) and the ordering rules of §11.5.6 (lowered via §24.4). For a simple `IdentifierPattern`, `StaticName(binding)` is the bound identifier and the static is emitted under that mangled symbol (`StaticSym`); destructuring patterns spread the binding across each pattern name in `StaticBindList(binding) = PatNames(pat)`. A purely literal initializer can be encoded as a constant (`ConstInit`); non-literal initializers run in the module init function. Eager static initializers participate in the project's initialization dependency graph and **must not form a cycle** (see §6.5, `E-MOD-1401`).

Naming follows the style guide's constant convention — **`SCREAMING_SNAKE`** for module-scope and static values (e.g. `MAX_SUBTICKS`, `DEFAULT_TIMEOUT_MS`), and **`_SCREAMING_SNAKE`** for private ones (e.g. `_FRAME_POOL_SIZE`, `_DEFAULT_LAYER_MASK`).

#### Diagnostics

| Code | Severity | Condition |
| --- | --- | --- |
| `E-TYP-1505` | Error | Missing required type annotation at module scope (`WF-StaticDecl-MissingType`). |
| `E-MOD-2402` | Error | Annotation incompatible with inferred type (`WF-StaticDecl-Ann-Mismatch`). |
| `E-MOD-2433` | Error | Module-scope `var` declared `public` (`StaticVisOk-Err`). |

#### Worked Example

```ultraviolet
public let MAX_SUBTICKS: u32 = 8
public let DEFAULT_TIMEOUT_MS: u64 = 5000

internal var _FRAME_POOL_SIZE: usize = 256

private let _DEFAULT_LAYER_MASK: u32 = 0xFFFF_FFFF
```

`MAX_SUBTICKS` and `DEFAULT_TIMEOUT_MS` are public immutable constants — each carries the required type annotation and uses `SCREAMING_SNAKE`. `_FRAME_POOL_SIZE` is a mutable static; it is deliberately `internal` (not `public`, which would be `E-MOD-2433`) and uses the `_SCREAMING_SNAKE` private-static form. `_DEFAULT_LAYER_MASK` is a `private let`. The following would not compile:

```ultraviolet
// E-TYP-1505: missing required type annotation at module scope.
let RETRY_LIMIT = 3

// E-MOD-2433: public mutable module-scope state is forbidden.
public var SHARED_COUNTER: u64 = 0
```

---

### 6.4 The Extern Block Shell (§11.4)

The `extern` block is the module-level **container** for foreign procedures. It establishes the ABI and linkage context that the contained foreign procedures share. This chapter covers the *shell* — the outer block, its ABI tag, and its block-level well-formedness. The detailed syntax, parsing, and AST form of the contained `extern_procedure_decl` / `foreign_procedure`, together with signature admissibility and FFI boundary rules, are owned by the FFI chapter (§23.2).

#### Syntax

```ebnf
extern_block ::= attribute_list? visibility? "extern" extern_abi? "{" extern_item* "}"
extern_abi   ::= string_literal | identifier
extern_item  ::= extern_procedure_decl
```

The Appendix B restatement constrains the ABI to a string literal in the foreign-procedure-bearing form, and gives the contained item grammar:

```ebnf
extern_block      ::= attribute_list? visibility? "extern" abi_string? "{" extern_item* "}"
abi_string        ::= string_literal
extern_item       ::= foreign_procedure
foreign_procedure ::= attribute_list? visibility? "procedure" identifier generic_params?
                      signature contract_clause?
                      foreign_contract_clause_list? terminator
```

The `extern_abi?` is optional; when present, the §11.4 parsing accepts either a string literal (e.g. `"C"`) — parsed as `StringAbi` — or a bare identifier — parsed as `IdentAbi`. An absent ABI tag is `⊥` (`abi_opt ∈ {⊥} ∪ ExternAbi`, `ExternAbi ∈ {StringAbi, IdentAbi}`).

#### Semantics

An `extern` block introduces **no direct runtime mechanism**; the block shell contributes ABI and linkage context for the procedures it contains, and runtime boundary behavior is defined by the FFI chapter (§11.4.5–§11.4.6 / Chapter 23).

The only block-level static check the shell owns is **ABI validity**, rule **(WF-ExternBlock)**:

```text
item = ExternBlock(_, _, abi_opt, _, _, _)    ExternAbiOk(abi_opt)
──────────────────────────────────────────────────────────────────
Γ ⊢ ExternBlock : ok
```

When `ExternAbiOk(abi_opt)` does not hold, rule **(ExternAbi-Unknown-Err)** fires. `ExternAbiOk` itself — the set of accepted ABI strings — is defined by §23.2.4, and unsupported-ABI-string rejection plus the signature diagnostics of the contained foreign procedures are owned by §23.2.7 and the FFI chapter. The shell does not validate procedure signatures.

#### Worked Example (shell only)

```ultraviolet
extern "C" {
    procedure clock_gettime(clock_id: i32, out_time: *mut TimeSpec) -> i32
}
```

The block establishes the `"C"` ABI for the single foreign procedure it contains. The raw-pointer parameter `out_time: *mut TimeSpec` uses the `raw_pointer_type ::= "*" raw_pointer_qual type` form with `raw_pointer_qual ::= "imm" | "mut"`. Per the style guide, such blocks belong in dedicated FFI boundary modules and should be kept thin; safe project-level wrappers (re-establishing contracts and invariants) should be exposed instead of the raw foreign handles. See the **FFI** chapter for the full grammar and semantics of `foreign_procedure`, foreign contracts, and ABI-string admissibility.

---

### 6.5 Module and File Aggregation (§11.5)

This section explains how source files become modules, how module paths are formed and resolved, and how `import` coverage is enforced — the machinery that the previous sections lean on.

#### 6.5.1 Directories are modules; files aggregate

In Ultraviolet **a directory defines a module**, and **all `.uv` files in that directory belong to the same module**. There is no per-file module boundary and no surface syntax for declaring a module path — the mapping is derived from the directory layout (§11.5.1).

```ebnf
path        ::= identifier ("::" identifier)*
module_path ::= path
```

A module's path is computed by `ModulePath(d, S, A)` from the directory `d`, the assembly source root `S`, and the assembly name `A`: at the source root the path is just the assembly name (**Module-Path-Root**, `relative(d, S) = ε ⇒ A`); otherwise it is the assembly name followed by the directory components relative to the source root (**Module-Path-Rel**, `relative(d, S) = c_1 / … / c_n ⇒ A :: c_1 :: … :: c_n`).

**Aggregation order.** `ParseModule` reads the directory's compilation unit `U = [f_1, …, f_n]`, parses each file, and concatenates their items and module docs **in file order** (**ParseModule-Ok**):

```text
∀ i, Γ ⊢ ReadBytes(f_i) ⇓ B_i    Γ ⊢ LoadSource(f_i, B_i) ⇓ S_i    Γ ⊢ ParseFile(S_i) ⇓ F_i
──────────────────────────────────────────────────────────────────────────────────────────────────
Γ ⊢ ParseModule(p, S) ⇓ ⟨p, F_1.items ++ ··· ++ F_n.items, F_1.module_doc ++ ··· ++ F_n.module_doc⟩
```

Crucially, **name collection is order-independent** (`NameCollectAfterParse`, §11.5.4): the bindings a module exports do not depend on which file an item lives in or on the order in which files aggregate. Splitting a module across `SessionConfig.uv`, `Loop.uv`, and `FrameGraph.uv` produces exactly the same module as putting everything in one file. This is what makes the style guide's "split files by responsibility, keep them ~400 lines" rule safe — file boundaries are purely organizational. If any file fails to read, load, or parse, the whole module fails (`ParseModule-Err-Read` / `-Load` / `-Parse`); a `LoadSource` failure short-circuits and MUST NOT trigger `Tokenize`/`ParseFile` for that file.

#### 6.5.2 Well-formed module paths

Every component of a module path must be a valid identifier and must **not** be a reserved keyword (**WF-Module-Path-Ok**: `∀ comp ∈ p, (Γ ⊢ comp : Identifier) ∧ ¬ Keyword(comp)`). Violations:

- a keyword component → **WF-Module-Path-Reserved** (`E-MOD-1105`);
- a non-identifier component → **WF-Module-Path-Ident-Err** (`E-MOD-1106`);
- two distinct paths that fold to the same name (`p_1 ≠ p_2 ∧ Fold(p_1) = Fold(p_2)`, under NFC + case folding) → **collision**: a hard error during discovery (`Disc-Collision` / `WF-Module-Path-Collision`, `E-MOD-1104`) together with a `W-MOD-1101` warning for potential collisions on case-insensitive filesystems.

This is the spec basis for the naming guide's `PascalCase` directory/module convention: distinct modules must remain distinct after case folding, so casing alone may not separate two module paths.

#### 6.5.3 Import coverage — the rule that ties §11.1 and §11.2 together

A reference (or a `using`) to an item in another **assembly** requires that assembly to be imported. The check is `ImportOk(m, path)`:

```text
ImportRequired(m, path) ⇔ AsmOfPath(path) ≠ AsmOfModule(m)
ImportCovers(m, path)   ⇔ ∃ p ∈ ImportPaths(m). ModulePathPrefix(p, path)
```

- **(Import-Ok-Local)** — if the path is in the current assembly (`¬ ImportRequired`), no import is required.
- **(Import-Ok-Covered)** — if an import is required and some import path is a prefix of the target, it is covered.
- **(Import-Ok-Err)** — if an import is required but no import covers it, fire `Import-Using-Missing` → **`E-MOD-1201`**.

Intra-assembly modules need **no** import to be referenced; cross-assembly modules **must** be imported, and the import path only needs to be a *prefix* (`ModulePathPrefix(p, path) ⇔ ∃ rest. path = p ++ rest`) of the target module path to cover it. This is precisely why every cross-assembly `using` in §6.2 was preceded by a matching `import`.

#### 6.5.4 Path resolution

Qualified references resolve by `ResolveModulePath` / `ResolveImportPath`: try the path as-written against the known module names first (`Resolve-…-Direct`), then retry with the current assembly prefixed via `CurrentAsmPath` (`Resolve-…-Current`); failure is `E-MOD-1107` (unresolved module, `ResolveModulePath-Err`) or `E-MOD-1202` (unresolved import, `Resolve-Import-Err`). Module aliases established by `import … as` are expanded first via `AliasExpand`, and when resolving a qualified name the **longest matching module prefix** wins (`ModulePrefix-Direct` selects `p = argmax_{q} |q|` over module prefixes of the path).

#### 6.5.5 Initialization ordering

Module aggregation builds the eager static-initialization dependency graph `G_e = ⟨V, E_val^{eager}⟩` (consumed by §24.4.2). Eager static initializers across modules must form a **DAG** (**WF-Acyclic-Eager**: `∀ v ∈ V. ¬ Reachable(v, v, E_val^{eager})`); a cycle is a cyclic module dependency, **`E-MOD-1401`**. This is the cross-module consequence of the static declarations in §6.3: a `let`/`var` whose eager initializer reads another module's eager static (an edge in `ValueDepsEager`) creates an init-order edge, and those edges may not loop. Initializers that run only lazily (record field initializers, procedure and method bodies) populate the *lazy* edge set `E_val^{lazy}` and are not subject to this acyclicity constraint.

#### Diagnostics

| Code | Severity | Condition |
| --- | --- | --- |
| `E-MOD-1104` | Error | Module path collision after NFC + case folding (`Disc-Collision`, `WF-Module-Path-Collision`). |
| `E-MOD-1105` | Error | Module path component is a reserved keyword (`WF-Module-Path-Reserved`). |
| `E-MOD-1106` | Error | Module path component is not a valid identifier (`WF-Module-Path-Ident-Err`). |
| `E-MOD-1201` | Error | External `using`/reference without a covering `import` (`Import-Using-Missing`). |
| `E-MOD-1107` | Error | Unresolved module: path prefix did not resolve to a module (`ResolveModulePath-Err`). |
| `W-MOD-1101` | Warning | Potential module path collision on a case-insensitive filesystem. |
| `E-MOD-1401` | Error | Cyclic module dependency detected in eager initializers. |

#### Worked Example (layout)

A `Grimoire` assembly with source root `Source/` and these directories:

```text
Source/Frame/Loop/          ->  module  Grimoire::Frame::Loop
Source/Inkwell/FrameGraph/  ->  module  Grimoire::Inkwell::FrameGraph
```

The directory `Source/Frame/Loop/` may contain several files that all aggregate into the one module `Grimoire::Frame::Loop`:

```ultraviolet
// Source/Frame/Loop/SessionConfig.uv
public record FrameRequest {
    public frame_index: u64
}
```

```ultraviolet
// Source/Frame/Loop/Step.uv
public enum FrameReply {
    Ready
    Deferred
}

public procedure step(request: FrameRequest) -> FrameReply {
    return FrameReply::Ready
}
```

Both files contribute to `Grimoire::Frame::Loop`; `FrameRequest`, `FrameReply`, and `step` are mutually visible without any `import` or `using`, because name collection over a module is order- and file-independent. The variant is referenced as `FrameReply::Ready` (an `enum_literal ::= type_path "::" identifier`), not with `.`.

---

### Idioms & Best Practices

Grounded in the style guide's *Imports and Visibility* and *Module-Scope State* sections:

- **Order imports from foundational to specific.** Foundational and built-in imports first, then engine/project imports, then aliases last. If an implementation module uses `using module::*`, place it **after** all regular imports and regular `using` declarations.
- **Write visibility explicitly.** Although omitted visibility defaults to `internal` (**Parse-Vis-Default**), project code must state `public`/`internal`/`private` on every module-level form. Treat visibility as part of the API contract, not an optional decoration.
- **Restrict wildcard `using`.** `using module::*` is allowed only in internal/implementation modules; never in public API modules. The spec enforces a softer version of this — `W-MOD-1201` fires whenever a wildcard `using` appears in a module that declares any `public` item (`PublicAPI(m)`) — so a wildcard in a public-API module is always a warning. Prefer exact-name or list `using` in public-facing code.
- **Alias only when it earns its keep.** Use `import … as …` and `using … as …` only when the alias avoids a real collision or meaningfully improves clarity (as `RenderGraph`/`RenderReply` did). Do not use renaming to simulate shadowing or ownership changes.
- **Keep module-scope state immutable.** Prefer `let` statics; reserve `var` for carefully justified runtime services or boundary objects. `public var` is outright forbidden by the language (`E-MOD-2433`). Name statics `SCREAMING_SNAKE`, private statics `_SCREAMING_SNAKE`.
- **Always annotate static types.** Module-scope bindings require an explicit type (`E-TYP-1505` otherwise) — there is no inference from surrounding context at module scope, and the annotation must be a supertype of the initializer (`E-MOD-2402` otherwise).
- **Organize by directory, split by responsibility.** Because files aggregate into one module with order-independent name collection, split a growing module into multiple `.uv` files (or a submodule directory) freely; keep files around ~400 lines. Use `PascalCase` directory/file names so module paths stay distinct under case folding. Isolate `extern` blocks to dedicated FFI boundary modules and keep them thin.

### Pitfalls & Diagnostics

- **`import` binds a module alias, not items.** After `import Grimoire::Frame::Loop`, you have the name `Loop` as a module alias — you still write `Loop::step` or add a `using` to get bare names. Expecting `import` to bring `step` into scope is the most common mistake.
- **Cross-assembly `using` without a covering `import` is `E-MOD-1201`.** A `using` (or qualified reference) into another assembly requires an `import` whose path is a prefix of the target. Intra-assembly references need no import at all.
- **`public using` of a non-public item is `E-MOD-1205`.** You cannot widen visibility through re-export: a `public using` may only re-export items that are themselves `public`.
- **Duplicate bound names in a `using` list are `E-MOD-1206`.** Distinctness is checked on the *bound* names after aliasing, so renaming two different items to the same alias collides exactly like importing the same name twice.
- **Wildcard `using` in a public-API module warns (`W-MOD-1201`).** It still compiles, but the warning is the spec telling you to use an explicit list or single-item `using` instead.
- **Missing static type annotation is `E-TYP-1505`; annotation/initializer mismatch is `E-MOD-2402`.** Always annotate, and ensure the initializer's type is a subtype of the annotation.
- **`public var` at module scope is `E-MOD-2433`.** Make it `internal`/`private`, or make it a `let`.
- **Module path collisions are `E-MOD-1104` (with `W-MOD-1101` on case-insensitive filesystems).** Two module paths that fold to the same name under NFC + case folding collide; reserved-keyword components are `E-MOD-1105` and non-identifier components are `E-MOD-1106`.
- **Cyclic eager static initializers are `E-MOD-1401`.** Module-scope `let`/`var` eager initializers that read each other across modules must form a DAG; break the cycle or move a dependency to a lazy position (a record field initializer or a procedure body).
- **Method calls use `~>`, not `.`.** The `.` postfix is field/tuple access only; an instance method is invoked as `receiver~>method(args)`, and an associated/path-qualified call is `Type::method(args)`. Enum variants are named with `::` (`FrameReply::Ready`), never `.`.

For accessibility rules (`public`/`internal`/`private` semantics) see the **Visibility & Accessibility** chapter (Chapter 7); for the full `foreign_procedure` grammar, foreign contracts, and ABI admissibility see the **FFI** chapter (§23.2); for static initialization and deinitialization lowering see the **Module Lifecycle** chapter (§24.4); and for the block-local `using … as …` statement (`using_local_stmt`) see the **Statements** chapter (§18.3).
