## 3. Projects, Manifest, Modules & the Compilation Model

An Ultraviolet *project* is the unit the compiler loads, validates, and turns into object files and a final linked artifact. A project is always rooted at a directory that contains an `Ultraviolet.toml` manifest. There is no single-file compilation mode: the manifest is mandatory, and the compiler must never fall back to a heuristic or single-file project (§3.2). This chapter defines the project record, the manifest schema, how assemblies are discovered and selected, how the filesystem maps to the module tree, what artifacts are emitted, how external tools are resolved, and every project-level diagnostic.

Throughout, `R` denotes the project root (the directory holding `Ultraviolet.toml`), `S` denotes an assembly's resolved source root, `P` denotes the loaded `Project` record, and `O` denotes the output root. Module-path formation and module discovery are owned by §11.5.4 of the specification; this chapter reproduces the filesystem-level relations they consume. For the module/file mapping at the language-surface level, see also the chapter on **Modules, Imports & Visibility**.

### 3.1 Core Project Records

The compiler models a project with two enumerations and one aggregate record. Backtick-quoted tokens are literal manifest values.

```text
AssemblyKind = {`executable`, `library`, `dependency`}
LinkKind     = {`shared`, `static`}

Project = ⟨root, assemblies, assembly, source_root, outputs, modules, toolchain, build⟩
```

`Project` carries the project root, the full list of discovered assemblies, the single *selected* assembly (`P.assembly`), and that assembly's source root, outputs, and module list, plus the resolved `toolchain` and `build` configuration tables. Accessors and predicates:

```text
Assemblies(P) = P.assemblies
Assembly(P)   = P.assembly
AsmNames(P)   = [A.name | A ∈ Assemblies(P)]
AsmByName(P, n) = A   ⇔ A ∈ Assemblies(P) ∧ A.name = n
                        ∧ (∀ B ∈ Assemblies(P). B.name = n ⇒ B = A)

Executable(P)    ⇔ P.assembly.kind = `executable`
Library(P)       ⇔ P.assembly.kind = `library`
Dependency(P)    ⇔ P.assembly.kind = `dependency`
Linkable(P)      ⇔ Executable(P) ∨ Library(P)
SharedLibrary(P) ⇔ Library(P) ∧ P.assembly.link_kind = `shared`
StaticLibrary(P) ⇔ Library(P) ∧ P.assembly.link_kind = `static`
```

The three assembly kinds have distinct roles:

- **`executable`** — produces a runnable binary. Requires exactly one `main` procedure with the entry signature (see below).
- **`library`** — produces a linked library artifact. Its `link_kind` selects `shared` (default) or `static`.
- **`dependency`** — produces *no* final linked artifact of its own. Its modules are emitted into the nearest importing linkable assembly selected by the build graph (§3.6). Use it for code that several assemblies share but that should never link on its own.

`Linkable(P)` is the union of executables and libraries; only linkable assemblies own a primary artifact. A `dependency` assembly is emitted (object files, and IR when requested) but never linked or archived in isolation.

#### Validation scope

Project loading and source compilation own disjoint rule sets. Phase-0 (project/manifest) checks are rules in chapter "3"; source checks belong to chapters 4 through 23. These two sets never overlap:

```text
Phase0Checks = RulesIn({"3"})
SourceChecks = RulesIn({"4", …, "23"})
Phase0Checks ∩ SourceChecks = ∅
```

This means a manifest error (for example a malformed `[toolchain]` table) is reported by an `E-PRJ-*` / `E-CLI-*` diagnostic before any source is parsed, and a source error never masquerades as a project error.

#### The entry procedure

For an `executable` assembly the program must define exactly one `main` whose signature is fixed:

```text
MainSigOk(d) ⇔ d = ProcedureDecl(_, vis, `main`, _, _, params, ret_opt, _, _, _, _)
             ∧ vis = `public`
             ∧ params = [⟨mode, name, ty⟩]            (exactly one parameter)
             ∧ mode ∈ {⊥, `move`}
             ∧ ContextBundleType(StripPerm(ty))        (the `Context` bundle, or a projected record)
             ∧ ret_opt = TypePrim("i32")
```

`main` must be `public`, non-generic, take a single parameter whose stripped type is the `Context` bundle (either the built-in `Context`, or a record whose fields project capability fields of `Context`), and return `i32`. The parameter mode may be omitted or `move`; no other mode is permitted. `Context` is a built-in record (it is a `BuiltinRecord` and a special type name), so it needs no `import`. The standard form:

```ultraviolet
//! Application entry point.

/// Program entry. Returns the process exit code.
public procedure main(context: Context) -> i32 {
    return 0
}
```

Non-executable assemblies skip the `main` check entirely (`Main-Bypass-NonExecutable`). Violations are reported by `E-MOD-2430` (multiple `main`, `Main-Multiple`), `E-MOD-2431` (bad signature, `Main-Signature-Err`), `E-MOD-2432` (generic `main`, `Main-Generic-Err`), and `E-MOD-2434` (missing `main`, `Main-Missing`); these codes are owned by the procedures chapter, but each is gated on `Executable(P)`, which is a project-level fact.

#### Command-line output and diagnostics

The compiler can summarize a loaded project. `DumpProject(P, dump)` emits a `ProjectSummary`, an `OutputSummary`, and a `LinkOutputSummary`; when `dump = true` it additionally lists every compilation-unit file:

```text
ProjectSummary(P) = [⟨project_root, P.root⟩, ⟨assemblies, AsmNames(P)⟩,
                     ⟨assembly_name, P.assembly.name⟩, ⟨assembly_kind, P.assembly.kind⟩,
                     ⟨link_kind, P.assembly.link_kind⟩, ⟨source_root, P.source_root⟩,
                     ⟨output_root, OutputRoot(P)⟩, ⟨module_list, ModuleList(P)⟩]
OutputSummary(P)   = [⟨module, m, obj, ObjPath(P, m), ir, IROpt(P, m)⟩ | m ∈ EmitModuleList(P)]
LinkOutputSummary(P) = [⟨artifact, PrimaryArtifact(P), import_lib, ImportLibOpt(P)⟩]  if Linkable(P)
                       []                                                            otherwise
```

`IROpt(P, m)` is the IR path only when `P.assembly.emit_ir ≠ none`; otherwise it is `⊥`. `ImportLibOpt(P)` is the import-library path only when one exists. Three command-line diagnostics are owned by `uv` command parsing, before any project-specific owner applies:

| Code         | Severity | Condition                                    |
| ------------ | -------- | -------------------------------------------- |
| `E-CLI-0001` | Error    | Unknown `uv` command                         |
| `E-CLI-0002` | Error    | Compiler pipeline unavailable for command    |
| `E-CLI-0003` | Error    | Failed to write command output or diagnostic |

### 3.2 The `Ultraviolet.toml` Manifest

#### Project-root discovery

When loading begins from one command-line input path `p`, the implementation first resolves a relative `p` against the host current working directory. If the resolved path is an existing non-directory, the upward search starts at its parent; otherwise it starts at the resolved path itself. `FindProjectRoot(p)` is the nearest searched directory `R` with `exists(R/Ultraviolet.toml)`; if no searched directory qualifies, it is the search-start directory.

#### Parsing the manifest

```text
ParseManifest(R) ⇓ T   when ParseTOML(R/Ultraviolet.toml) ⇓ T
ParseManifest(R) ⇑ c   when ¬ exists(R/Ultraviolet.toml)         (c = Parse-Manifest-Missing → E-PRJ-0101)
ParseManifest(R) ⇑ c   when ParseTOML(R/Ultraviolet.toml) ⇑      (c = Parse-Manifest-Err     → E-PRJ-0102)
```

Two hard rules:

- **Manifest required, no fallback.** If `ParseManifest(R)` fails with code `c`, then `LoadProject(R, target)` fails with `c`, and the implementation MUST NOT attempt any single-file or heuristic fallback project construction.
- **Path resolution.** Manifest lookup uses host filesystem path resolution for `R/Ultraviolet.toml` and must not perform additional case verification.

#### Top-level schema

```text
TopKeys = {assembly, toolchain, build}
```

Only those three top-level keys are permitted. Any other top-level key is `WF-TopKeys-Err` (`E-PRJ-0104`). The `assembly` field may be a single table or an array of tables; it must contain at least one assembly:

```text
AsmTables(T) = [AsmField(T)]   if IsTable(AsmField(T))         (one [assembly] table)
               AsmField(T)     if IsArrayTable(AsmField(T))    (an [[assembly]] array)
               ⊥               otherwise
```

A missing/ill-typed `assembly` field is `WF-Assembly-Table-Err`; zero assemblies is `WF-Assembly-Count-Err` (both `E-PRJ-0103`). Assembly names must be distinct across the manifest (`WF-Assembly-Name-Dup` → `E-PRJ-0202`).

#### The `[assembly]` table

```text
Req = {name, kind, root}
Opt = {out_dir, emit_ir, link_kind}
```

Each assembly table may contain only keys in `Req ∪ Opt`; unknown keys are `WF-Assembly-Keys-Err` (`E-PRJ-0104`). Required keys must all be present and of string type (`WF-Assembly-Required-Types-Err` → `E-PRJ-0103`). Per-field rules:

| Key         | Required | Type   | Allowed values / constraint                                                | Bad-value diagnostic |
| ----------- | -------- | ------ | -------------------------------------------------------------------------- | -------------------- |
| `name`      | yes      | string | a valid identifier that is not a keyword (`WF-Assembly-Name`)              | `E-PRJ-0203`         |
| `kind`      | yes      | string | one of `executable`, `library`, `dependency` (`AssemblyKind`)             | `E-PRJ-0201`         |
| `root`      | yes      | string | a *relative* path that resolves under `R` (`RelPath`)                     | `E-PRJ-0301`         |
| `out_dir`   | no       | string | `⊥` or a relative path under `R`                                          | `E-PRJ-0301`         |
| `emit_ir`   | no       | string | one of `none`, `ll`, `bc` (or `⊥`)                                        | `E-PRJ-0204`         |
| `link_kind` | no       | string | `shared` / `static`, **only** when `kind = library`                      | `E-PRJ-0207`/`0208`  |

`link_kind` is governed precisely:

```text
AsmLinkKind(k, l) = `shared`  if k = `library` ∧ l = ⊥
                    l         if k = `library` ∧ l ∈ {`shared`, `static`}
                    ⊥         otherwise
```

A bad `link_kind` *value* on a library (a string that is neither `shared` nor `static`, or a non-string type) is `WF-Assembly-LinkKind-Err` / `WF-Assembly-LinkKindType-Err` (`E-PRJ-0207`). Setting `link_kind` at all on an `executable` or `dependency` is `WF-Assembly-LinkKind-Use-Err` (`E-PRJ-0208`). A library defaults to `shared` when `link_kind` is omitted.

`root` and `out_dir` are validated as relative paths that, once resolved against `R`, stay under `R`. An absolute path, a path that escapes the root via `..`, or a non-string value is rejected (`WF-Assembly-Root-Path-Err` / `WF-Assembly-OutDir-Path-Err` / `WF-Assembly-OutDirType-Err` → `E-PRJ-0301`). Path validation uses the canonicalizing relation:

```text
RelPath:  is_relative(p) ∧ Resolve(R, p) ⇓ (R', p'') ∧ prefix(p'', R')
```

Resolution itself can fail on filesystem error or on an uncanonicalizable `..` (`Resolve-Canonical-Err` → `E-PRJ-0304`). After resolution, the `root` directory must actually exist and be a directory (`WF-Source-Root-Err` → `E-PRJ-0302`).

#### The `[toolchain]` table

```text
ToolchainKeys = {llvm_bin, runtime_lib, target_profile}
```

All three keys are optional. `llvm_bin` and `runtime_lib` are strings (paths). `target_profile`, when present, must be a string drawn from the fixed set of target profiles:

```text
TargetProfile = {`x86_64-sysv`, `x86_64-win64`, `aarch64-aapcs64`, `aarch64-darwin`}
ToolchainTargetProfileOk(v) ⇔ v = ⊥ ∨ (v : string ∧ v ∈ TargetProfile)
```

Any unknown toolchain key, wrong value type, or out-of-set `target_profile` is `WF-Toolchain-Err` (`E-PRJ-0110`). When `[toolchain]` is absent every field defaults to `⊥`:

```text
ToolchainConfig(T) = ⟨llvm_bin, runtime_lib, target_profile⟩            if T[toolchain] ≠ ⊥
                     ⟨llvm_bin = ⊥, runtime_lib = ⊥, target_profile = ⊥⟩  otherwise
```

**Target-profile selection.** The selected target profile is resolved once per compilation invocation, in this order (no host inference is ever allowed):

```text
SelectedTargetProfile(cli, T) =
   cli                                if cli ≠ ⊥                                  (1) CLI override
   ToolchainConfig(T).target_profile  if cli = ⊥ ∧ target_profile ≠ ⊥            (2) manifest
   error                              otherwise                                   (3) ill-formed
```

If neither a CLI override nor `[toolchain].target_profile` supplies a profile, the invocation is ill-formed and reports `E-PRJ-0112`. A conforming implementation must not silently infer the profile from the host platform. `SelectedTargetProfile` drives every platform-dependent artifact decision in §3.6 (object/exe/lib suffixes, library prefix, linker and archiver tool names, runtime-lib name, and whether a shared library also emits an import library).

#### The `[build]` table

```text
BuildKeys = {incremental, progress}
```

Both keys are optional booleans; any unknown key or non-bool value is `WF-Build-Err` (`E-PRJ-0111`). Defaults:

```text
BuildConfig(T) = ⟨incremental = false, progress = true⟩         if [build] absent
                 ⟨incremental = b, progress = (p else true)⟩     otherwise
```

`incremental` defaults to `false`; `progress` defaults to `true` even when `[build]` is present but omits it.

#### Worked manifest

```toml
# Ultraviolet.toml — an executable that links a static library and shares a dependency.

[[assembly]]
name    = "Grimoire"
kind    = "executable"
root    = "Source"

[[assembly]]
name      = "Vellum"
kind      = "library"
root      = "Vellum/Source"
link_kind = "static"

[[assembly]]
name = "Shared"
kind = "dependency"
root = "Shared/Source"

[toolchain]
target_profile = "x86_64-win64"

[build]
incremental = true
progress    = true
```

A minimal single-assembly manifest needs only one `[[assembly]]` (or `[assembly]`) table with `name`, `kind`, `root`. The target profile may be supplied here under `[toolchain]` or on the command line.

### 3.3 Assemblies and Project Loading

Loading is a deterministic small-step process. Its states:

```text
ProjLoadState = {Start(R, target), Parsed(R, target, T), Validated(R, target, T),
                 ProjAsmScan(R, target, T, Ts, As), Discovered(P), Error(code)}
```

The pipeline, in order: **parse** the manifest → **validate** it → **build** each assembly → **own** modules across assemblies → **select** the target assembly → `Discovered(P)`.

#### Deterministic manifest validation

Validation runs a fixed checklist and reports the *first* failing check (`FirstFail`), so the diagnostic is stable regardless of how many problems exist:

```text
BaseChecks(T) = [T : TopKeys, T : AssemblyTable, T : AssemblyCount, T : AssemblyNames]
ChecksAsm(t)  = [t : KnownKeys, t : ReqTypes, t : OutDirType, t : EmitIRType, t : LinkKindType,
                 t.name : Name, t.kind : Kind, t : LinkKindField, t.emit_ir : EmitIR,
                 t.root : RootPath, t.out_dir : OutDirPath]
Checks(T)     = BaseChecks(T) ++ (++_{t ∈ AsmTables(T)} ChecksAsm(t))
```

Base checks precede per-assembly checks; within an assembly, key/type checks precede value checks. `ValidateManifest(T)` succeeds iff `FirstFail(Checks(T)) = ⊥`.

#### Building one assembly

For each assembly table `t`, `BuildAssembly(R, t)` resolves `t.root` against `R`, confirms it is a directory, discovers the module set under it, sorts modules by emission order, and records output paths:

```text
BuildAssembly(R, t) ⇓ A   where
   Resolve(R, t.root) ⇓ (R', S) ∧ S : SourceRoot
   Modules(S, t.name) ⇓ M ∧ L = sort_{≺_mod}(M)
   A = ⟨name = t.name, kind = t.kind, link_kind = AsmLinkKind(t.kind, t.link_kind),
        root = t.root, out_dir = t.out_dir, emit_ir = t.emit_ir,
        source_root = S, outputs = OutputPaths(R, t), modules = L⟩
```

Failure to resolve `root` (`Resolve-Canonical-Err`), a non-directory `root` (`WF-Source-Root-Err`), or a module-discovery error each propagate as the corresponding diagnostic.

#### Module ownership across assemblies

When several assemblies share an enclosing directory tree, every module directory must belong to exactly one source root — the *deepest* root that is a prefix of it. `OwnerRoot(d, Rs)` picks that deepest, unique owning root; `OwnedModules` filters each assembly down to the modules it owns:

```text
RootDepth(S)     = |PathComps(S)|
OwnerRoot(d, Rs) = S_k   ⇔ S_k ∈ Rs ∧ prefix(d, S_k)
                            ∧ (∀ S_i ∈ Rs. prefix(d, S_i) ⇒ RootDepth(S_i) ≤ RootDepth(S_k))
                            ∧ (∀ S_j ∈ Rs. prefix(d, S_j) ∧ RootDepth(S_j) = RootDepth(S_k) ⇒ S_j = S_k)
OwnedModules(A, As) = [ m ∈ A.modules
                        | OwnerRoot(ModuleDirOf(m, A.source_root), AssemblySourceRoots(As)) = A.source_root ]
```

If any module directory has no unique deepest owner, loading fails with `WF-Assembly-Root-Owner-Ambiguous` (`E-PRJ-0206`). The practical rule: do not give two assemblies the *same* `root`, and if one assembly's `root` nests inside another's, the inner root cleanly claims everything beneath it.

#### Selecting the target assembly

`target` is the optional assembly name supplied on the command line. Selection:

```text
Select-Only      |As| = 1 ∧ target = ⊥                         → the sole assembly
Select-Only-Exe  |As| > 1 ∧ target = ⊥ ∧ exactly one executable → that executable
Select-By-Name   target ≠ ⊥ ∧ A.name = target                  → A
Select-Err       otherwise                                     → Assembly-Select-Err (E-PRJ-0205)
```

So with multiple assemblies and no explicit target, the build picks the unique executable; if there is no unique executable you must name the assembly, or selection fails.

#### Worked load (skeleton)

```ultraviolet
//! Grimoire — module Grimoire (root module of the executable assembly).

/// Program entry. Boots the session and returns the process exit code.
public procedure main(context: Context) -> i32 {
    let exit_code: i32 = bootSession(context)
    return exit_code
}
```

```ultraviolet
//! Session boot logic for the Grimoire executable.

/// Boot the session and return the resulting process exit code.
public procedure bootSession(context: Context) -> i32 {
    return 0
}
```

Both files live in `Source/` (the assembly root), so both belong to the single root module `Grimoire` (see §3.5). Because the module spans both files, either file may carry the `//!` module documentation.

#### Assembly graph constraints

Imports between assemblies (the first path segment of each `import` names an assembly) form a graph with three constraints, enforced by `AssemblyGraph(P)` before linking (§3.6):

1. A linkable assembly reachable to an `executable` is ill-formed. `ImportsExecutable(P)` ⇒ `Assembly-Graph-Err` (`E-PRJ-0209`).
2. The library link graph must be acyclic. A cycle reachable through linked libraries ⇒ `Assembly-Graph-Err` (`E-PRJ-0209`).
3. A `dependency` assembly must not own a final linked artifact; its modules emit into the nearest importing linkable assembly.

A **hosted library** — a `library` assembly that carries at least one `#host_export` procedure, so `HostedLibrary(P) ⇔ Library(P) ∧ HostExports(P) ≠ []` — that reaches another linked `library` assembly is `Assembly-Graph-HostedImport-Err` (`E-PRJ-0210`).

### 3.4 Deterministic Ordering and Case Folding

Every directory scan and file enumeration the compiler performs is sorted, so the module list, emission order, and object layout are byte-for-byte reproducible across hosts and filesystems.

Ordering keys fold each path component to its case-insensitive, NFC-normalized form, then break ties on the original bytes:

```text
FoldPath(r) = JoinComp([CaseFold(NFC(c)) | c ∈ PathComps(r)])
Fold(p)     = [CaseFold(NFC(c)) | c ∈ p]
```

Files within a directory are ordered by `≺_file`; directories under a source root by `≺_dir`:

```text
FileKey(f, d) = ⟨FoldPath(rel), rel⟩  if relative(f, d) ⇓ rel
                ⟨⊥, Basename(f)⟩       if relative(f, d) ⇑
f_1 ≺_file f_2 ⇔ Utf8LexLess(FileKey(f_1, d), FileKey(f_2, d))

DirKey(d, S)  = ⟨FoldPath(rel), rel⟩  if relative(d, S) ⇓ rel
                ⟨⊥, Basename(d)⟩       if relative(d, S) ⇑
d_1 ≺_dir d_2 ⇔ Utf8LexLess(DirKey(d_1, S), DirKey(d_2, S))

DirSeq(S) = sort_{≺_dir}(Dirs(S))
```

Both keys are *pairs*: the folded path first (the primary, case-insensitive sort), then the raw relative path (a stable tiebreaker so two components differing only by case never compare equal arbitrarily). A directory or file whose relative path cannot be derived raises `DirSeq-Rel-Fail` / `FileOrder-Rel-Fail` (`E-PRJ-0303`); a directory listing that cannot be read raises `DirSeq-Read-Err` (`E-PRJ-0305`).

Module *emission* order (§3.6) uses the same fold-then-raw structure on the full module path:

```text
m_1 ≺_mod m_2 ⇔ Utf8LexLess(Fold(path(m_1)), Fold(path(m_2)))
              ∨ (Fold(path(m_1)) = Fold(path(m_2)) ∧ Utf8LexLess(path(m_1), path(m_2)))

Utf8LexLess(a, b) ⇔ LexBytes(Utf8(a), Utf8(b))
```

Case folding is also why two module directories whose paths fold to the same key collide: see `Disc-Collision` in §3.5. That collision is `E-MOD-1104` (owned by the modules chapter), and on a case-insensitive filesystem the compiler additionally emits the warning `W-MOD-1101`.

### 3.5 Source Roots, Module Directories, and Compilation Units

This is the heart of the compilation model. **In Ultraviolet, directories define modules. A file name is never a module boundary.**

#### Source root

An assembly's `root` resolves to a source root `S`, which must be an existing directory:

```text
S : SourceRoot ⇔ is_dir(S)         (otherwise WF-Source-Root-Err → E-PRJ-0302)
Dirs(S) = { d | is_dir(d) ∧ relative(d, S) ⇓ r },  with  S ∈ Dirs(S)
```

`Dirs(S)` includes `S` itself, so the source root directory is a candidate module directory.

#### Module directories and compilation units

A directory is a *module directory* exactly when it directly contains at least one `.uv` file:

```text
(Module-Dir)   ∃ f ∈ Files(d) : FileExt(f) = ".uv"
               ─────────────────────────────────────
               Γ ⊢ d : ModuleDir

Files(d)           = { f | f ∈ d ∧ FileExt(f) = ".uv" }
CompilationUnit(d) = sort_{≺_file}(Files(d))
```

The **compilation unit** of a module directory is *all* `.uv` files directly in that directory, sorted by `≺_file`. Every one of those files contributes its items and its module documentation to the *same* module. Concretely, `ParseModule` reads each file in `CompilationUnit(d)` in order and concatenates their items and module docs:

```text
ParseModule(p, S) ⇓ ⟨p, F_1.items ++ ··· ++ F_n.items, F_1.module_doc ++ ··· ++ F_n.module_doc⟩
```

(If loading any file in the unit fails, `ParseModule` short-circuits and does not tokenize or parse the remaining files.) Files in *subdirectories* do not belong to the parent module — they belong to their own module directory. A relative-path failure while ordering a unit is `FileOrder-Rel-Fail` (`E-PRJ-0303`).

#### Mapping the filesystem to the module tree

For a module directory `d` under source root `S`, the module path is the assembly name followed by the relative directory components, joined with `::`:

```text
(Module-Path-Root)   relative(d, S) = ε                ⇒ ModulePath(d, S, A) = A
(Module-Path-Rel)    relative(d, S) = c_1 / … / c_n    ⇒ ModulePath(d, S, A) = A :: c_1 :: … :: c_n

ModuleDirOf(A, S)                    = S
ModuleDirOf(A :: c_1 :: … :: c_n, S) = S / c_1 / … / c_n    (n ≥ 1)
```

`ModuleDirOf` is the exact inverse: it recovers the directory from a module path. So the assembly-root directory `S` is the *root module* whose path is just the assembly name `A`; each subdirectory under `S` that contains `.uv` files is a submodule whose path appends the directory names.

Each path component must be a non-keyword identifier:

```text
WF-Module-Path(p):  ∀ comp ∈ p. (comp : Identifier) ∧ ¬ Keyword(comp)
```

A component that is a reserved keyword is `WF-Module-Path-Reserved` (`E-MOD-1105`); a component that is not a valid identifier is `WF-Module-Path-Ident-Err` (`E-MOD-1106`). Because directory names become module-path identifiers, name your directories in `PascalCase` and never use a reserved word (`type`, `loop`, `class`, …) as a directory name.

#### Module discovery

Discovery walks `DirSeq(S)` (all directories under the source root, sorted), skips non-module directories, and adds each module directory's path, detecting case-fold collisions:

```text
(Disc-Start)      ⟨DiscStart(S, A)⟩ → ⟨DiscScan(S, A, DirSeq(S), [], ∅)⟩
(Disc-Skip)       d not a ModuleDir                              → skip d
(Disc-Add)        d : ModuleDir, ModulePath = p, WF ok, Fold(p) unseen
                                                                 → add p, record Fold(p) ↦ p
(Disc-Collision)  d : ModuleDir, Fold(p) already recorded for a different p
                                                                 → Disc-Collision (E-MOD-1104)
(Disc-Invalid-Component)  WF-Module-Path(p) ⇑ c                  → c (E-MOD-1105 / E-MOD-1106)
(Disc-Rel-Fail)   relative(d, S) ⇑                               → Disc-Rel-Fail (E-PRJ-0303)
(Disc-Done)       no directories left                            → DiscDone(M)
```

Two directories whose paths fold to the same key (for example `Frame` and `frame`) are a collision error, `E-MOD-1104`; on a case-insensitive host the compiler additionally emits warning `W-MOD-1101`.

#### Worked layout

```text
Grimoire/
├── Ultraviolet.toml
└── Source/                     ← assembly root  →  module  Grimoire
    ├── Main.uv                 ┐ both files form the ONE module Grimoire
    ├── Boot.uv                 ┘ (same directory = same compilation unit)
    └── Frame/                  ← module  Grimoire::Frame
        ├── Loop.uv             ┐ Loop.uv + Schedule.uv = one module: Grimoire::Frame
        ├── Schedule.uv         ┘
        └── Graph/              ← module  Grimoire::Frame::Graph
            └── FrameGraph.uv
```

Mapping summary for this tree:

| Directory                    | Module path               | Compilation unit (sorted)        |
| ---------------------------- | ------------------------- | -------------------------------- |
| `Source/`                    | `Grimoire`                | `Boot.uv`, `Main.uv`             |
| `Source/Frame/`              | `Grimoire::Frame`         | `Loop.uv`, `Schedule.uv`         |
| `Source/Frame/Graph/`        | `Grimoire::Frame::Graph`  | `FrameGraph.uv`                  |

`Main.uv` and `Boot.uv` are the *same* module `Grimoire`; to split `Grimoire` into a submodule you create a subdirectory, not another file. Module-path formation, visibility, and import resolution are owned by §11.5.4 of the specification; this section supplies only the filesystem relations they consume.

### 3.6 Output Artifacts and Linking

#### Output root and directory layout

```text
OutputRoot(P) = P.root / P.assembly.out_dir   if out_dir provided
                P.root / Build                 otherwise
```

The output root defaults to `<root>/Build`, overridable per assembly with `out_dir`. Beneath it:

```text
O/Intermediate               ← scratch
O/Intermediate/Obj           ← object files
O/Intermediate/IR            ← emitted LLVM IR (only when emit_ir ∈ {ll, bc})
O/Intermediate/Incremental   ← incremental-build state
O/Binary                     ← executables and shared libraries
O/Library                    ← static libraries and import libraries
O/Logs
```

**Output hygiene.** Every required output path must stay under the output root: `∀ p ∈ RequiredOutputs(P). Under(p, OutputRoot(P))`.

#### Object-file naming

Object files are placed by the module's directory-relative path and named by a deterministic mangling of the module path:

```text
ModuleOutputRel(P, m) = Rel(ModuleDirOf(m, P.source_root), P.root)
MangleModulePath(p)   = mangle(PathString(PathKey(p)))     (NFC, then [0-9A-Za-z] kept, others → _xHH)
ObjPath(P, m) = O / Intermediate / Obj / ModuleOutputRel(P, m)
                  / (MangleModulePath(path(m)) ++ ObjExt(SelectedTargetProfile))
```

`ObjExt` is supplied by the selected target profile (`.obj` on `x86_64-win64`, `.o` on the others). IR paths mirror this under `Intermediate/IR` with `.ll` or `.bc`.

#### Final artifact naming

```text
libname = LibraryPrefix(SelectedTargetProfile) ++ assembly_name
exe     = O / Binary  / (assembly_name ++ ExeSuffix(SelectedTargetProfile))
shared  = O / Binary  / (libname ++ SharedLibSuffix(SelectedTargetProfile))
static  = O / Library / (libname ++ StaticLibSuffix(SelectedTargetProfile))
import  = O / Library / (libname ++ ImportLibSuffix(SelectedTargetProfile))
```

The library prefix and every suffix come from the target profile. The profile-keyed values are:

| Profile           | `ObjExt` | `ExeSuffix` | `LibraryPrefix` | `SharedLibSuffix` | `StaticLibSuffix` | `ImportLibSuffix` | `EmitsImportLib` |
| ----------------- | -------- | ----------- | --------------- | ----------------- | ----------------- | ----------------- | ---------------- |
| `x86_64-sysv`     | `.o`     | (empty)     | `lib`           | `.so`             | `.a`              | `.so.import`      | false            |
| `x86_64-win64`    | `.obj`   | `.exe`      | (empty)         | `.dll`            | `.lib`            | `.lib`            | true             |
| `aarch64-aapcs64` | `.o`     | (empty)     | `lib`           | `.so`             | `.a`              | `.so.import`      | false            |
| `aarch64-darwin`  | `.o`     | (empty)     | `lib`           | `.dylib`          | `.a`              | `.dylib.import`   | false            |

So the same project yields `Grimoire.exe` + `Vellum.lib` on `x86_64-win64`, but `Grimoire` + `libVellum.a` on a SysV profile. The primary artifact per kind:

```text
PrimaryArtifact(P) = ExePath(P)        if Executable(P)
                     SharedLibPath(P)  if SharedLibrary(P)
                     StaticLibPath(P)  if StaticLibrary(P)
                     ⊥                 otherwise   (dependency assemblies own no artifact)
```

A shared library additionally emits an import library when `EmitsImportLib(SelectedTargetProfile)` holds (only `x86_64-win64`). `UsesBinDir(P)` ⇔ executable or shared (they write to `Binary`); `UsesLibDir(P)` ⇔ static, or shared with an import library (they write to `Library`).

#### Which modules get emitted

The emit set is the selected assembly plus any `dependency` assemblies reachable from it without crossing a library boundary; libraries are built separately and linked in:

```text
EmitAssemblies(P) = { A | A = P.assembly
                          ∨ (A.kind = `dependency`
                             ∧ ∃ π. GraphPath(AsmImportGraph(P), π) ∧ π[0] = P.assembly
                                    ∧ last(π) = A ∧ NoLibraryInterior(π)) }
EmitModuleList(P) = sort_{≺_mod}(⋃_{A ∈ EmitAssemblies(P)} A.modules)
```

This is how a `dependency` assembly's modules fold into the importing linkable assembly. Imported libraries are built in dependency order (`LibraryTopo(P)`, a deterministic topological sort that breaks ties lexicographically by name) and their primary artifacts become link inputs (`LibraryArtifactInputs(P)`).

#### IR emission

```text
emit_ir = none  → no IR files
emit_ir = ll    → textual LLVM IR (.ll) per module
emit_ir = bc    → bitcode (.bc) per module; produced by assembling .ll with the resolved llvm-as tool
```

For `bc`, the compiler first generates `.ll`, resolves `llvm-as` (§3.7), and assembles it; any step failing yields `Out-IR-Err`. IR-path collisions yield `Out-IR-Collision`.

#### Linking, archiving, finalizing

The output pipeline ensures the directories exist, emits objects, emits IR, then finalizes. Finalizing **links** an executable or shared library and **archives** a static library:

```text
LinkMode(P)       = `exe`     if Executable(P);  `shared` if SharedLibrary(P)
LinkObjs(P)       = ObjPaths(P, EmitModuleList(P))
LinkInputs(P)     = LinkObjs(P) ++ LibraryArtifactInputs(P) ++ [RuntimeLibPath(P)]
ArchiveInputs(P)  = LinkObjs(P)
```

Linking requires, in sequence: the assembly graph is valid (`AssemblyGraph(P) ⇓ ok`), all imported libraries build (`BuildLibraries(P) ⇓ ok`), the linker tool resolves, and the runtime library resolves and is symbol-compatible. The failure codes:

| Code                            | Condition                                              |
| ------------------------------- | ------------------------------------------------------ |
| `Out-Link-NotFound`             | Linker or archiver tool not found                      |
| `Out-Link-Runtime-Missing`      | Runtime library could not be resolved/read             |
| `Out-Link-Runtime-Incompatible` | Runtime library is missing required symbols            |
| `Out-Link-Fail`                 | The linker or archiver invocation itself failed        |
| `Out-Obj-Collision`             | Two modules map to the same object path                |
| `Out-Dirs-Err`                  | An output directory could not be created               |

A `dependency` project runs the pipeline only up to object/IR emission and never finalizes (it produces `(Objs, IRs, ⊥)`), consistent with constraint 3 of §3.3.

#### Module symbol naming

```text
EmitModuleList(P) = [m_1, …, m_n]
Index(P, m_i)     = i
SymbolName(P, m)  = "main"                       if path(m) = P.assembly.name   (the root module)
                    "mod" ++ pad4(Index(P, m))   otherwise
```

The root module's symbol is `main`; every other module is `mod` plus its 4-digit emission index. Only an executable's root module additionally receives the entry stub (`WithEntry`).

### 3.7 Tool Resolution and IR Assembly Inputs

External tools — the linker, the archiver, and `llvm-as` — are resolved through a fixed search-directory precedence:

```text
SearchDirs(P) = [ToolchainConfig(P).llvm_bin]   if llvm_bin ≠ ⊥          (1) explicit
                [CompilerToolBinDir(P)]          if that dir exists      (2) packaged sidecar
                PATHDirs                          otherwise               (3) host PATH
```

`CompilerToolBinDir(P)` points at the host-specific `tools` directory beside the compiler when packaged sidecars are present (`windows/tools` for COFF, `macos/tools` for Mach-O, or `linux/tools` for ELF — chosen by the selected profile's object format), else a flat `tools` directory beside the compiler support root. Resolution:

```text
ResolveTool(x) ⇓ t   when SearchDirs(P) contains x at t
                     and, if x = llvm-as, ToolVersion(t) = LLVMToolchain
```

A missing linker or archiver yields `ResolveTool ⇑` (surfacing as `Out-Link-NotFound`); a missing or version-mismatched `llvm-as` fails IR assembly. The linker and archiver tool *names* themselves are chosen by `SelectedTargetProfile`:

| Profile           | `LinkerToolName` | `ArchiverToolName` |
| ----------------- | ---------------- | ------------------ |
| `x86_64-sysv`     | `ld.lld`         | `llvm-ar`          |
| `x86_64-win64`    | `lld-link`       | `llvm-lib`         |
| `aarch64-aapcs64` | `ld.lld`         | `llvm-ar`          |
| `aarch64-darwin`  | `clang++`        | `llvm-ar`          |

IR assembly for `emit_ir = bc` simply invokes the resolved `llvm-as` on the textual IR:

```text
AssembleIR(a, t) ⇓ b   when Invoke(a, t) ⇓ b
AssembleIR(a, t) ⇑     when Invoke(a, t) ⇑
```

**Runtime library resolution.** The runtime library path is the explicit `[toolchain].runtime_lib` when set, else a compiler-relative default (`<compiler-dir>/<RuntimeLibName>` when present, otherwise `<support-root>/runtime/<RuntimeLibName>`), where `RuntimeLibName` is profile-specific (`UltravioletRT.lib` on `x86_64-win64`, `UltravioletRT.a` on the others). Linking reads this library and checks it provides the required runtime symbols (`Out-Link-Runtime-Missing` / `Out-Link-Runtime-Incompatible`).

### 3.8 Project Diagnostics

This section owns the manifest, assembly-selection, source-root, deterministic-ordering, and project-discovery diagnostics. All are compile-time errors.

| Code         | Condition                                                                                                   |
| ------------ | ----------------------------------------------------------------------------------------------------------- |
| `E-PRJ-0101` | `Ultraviolet.toml` not found at project root (`Parse-Manifest-Missing`)                                     |
| `E-PRJ-0102` | `Ultraviolet.toml` is not valid TOML (`Parse-Manifest-Err`)                                                 |
| `E-PRJ-0103` | Missing `assembly` table, empty assembly list, missing required key, or wrong required-key type (`WF-Assembly-Table-Err`, `WF-Assembly-Count-Err`, `WF-Assembly-Keys-Err`, `WF-Assembly-Required-Types-Err`) |
| `E-PRJ-0104` | Unknown key in `assembly` table or unknown top-level key (`WF-TopKeys-Err`, `WF-Assembly-Keys-Err`)         |
| `E-PRJ-0110` | Invalid `[toolchain]` section (`WF-Toolchain-Err`)                                                           |
| `E-PRJ-0111` | Invalid `[build]` section (`WF-Build-Err`)                                                                   |
| `E-PRJ-0112` | No target profile selected by CLI override or `[toolchain].target_profile`                                  |
| `E-PRJ-0201` | `assembly.kind` not in `{executable, library, dependency}` (`WF-Assembly-Kind-Err`)                         |
| `E-PRJ-0202` | Duplicate `assembly.name` values (`WF-Assembly-Name-Dup`)                                                    |
| `E-PRJ-0203` | `assembly.name` is not a valid identifier (`WF-Assembly-Name-Err`)                                           |
| `E-PRJ-0204` | `emit_ir` has invalid value or type (`WF-Assembly-EmitIR-Err`, `WF-Assembly-EmitIRType-Err`)                |
| `E-PRJ-0205` | Assembly selection failed: missing target or target not found (`Assembly-Select-Err`)                       |
| `E-PRJ-0206` | Ambiguous assembly root ownership for overlapping source roots (`WF-Assembly-Root-Owner-Ambiguous`)         |
| `E-PRJ-0207` | `link_kind` has invalid value or type (`WF-Assembly-LinkKind-Err`, `WF-Assembly-LinkKindType-Err`)          |
| `E-PRJ-0208` | `link_kind` set when `assembly.kind ≠ library` (`WF-Assembly-LinkKind-Use-Err`)                             |
| `E-PRJ-0209` | Assembly graph imports an executable or cycles through linked libraries (`Assembly-Graph-Err`)              |
| `E-PRJ-0210` | Hosted library imports another linked library assembly (`Assembly-Graph-HostedImport-Err`)                  |
| `E-PRJ-0301` | `assembly.root` or `out_dir` is wrong-typed, absolute, or resolves outside root (`WF-Assembly-Root-Path-Err`, `WF-Assembly-OutDir-Path-Err`, `WF-Assembly-OutDirType-Err`) |
| `E-PRJ-0302` | `assembly.root` does not exist or is not a directory (`WF-Source-Root-Err`)                                 |
| `E-PRJ-0303` | Relative-path derivation failed during deterministic ordering or discovery (`DirSeq-Rel-Fail`, `Disc-Rel-Fail`, `FileOrder-Rel-Fail`, `WF-RelPath-Err`) |
| `E-PRJ-0304` | Path canonicalization or module-path derivation failed (`Resolve-Canonical-Err`)                            |
| `E-PRJ-0305` | Directory enumeration failed during module discovery (`DirSeq-Read-Err`)                                    |

Module-path collisions and bad path components surface under codes owned by the modules chapter, not under `E-PRJ-*`:

| Code         | Condition                                                                                        |
| ------------ | ------------------------------------------------------------------------------------------------ |
| `E-MOD-1104` | Module-path collision after NFC + case folding (`Disc-Collision`, `WF-Module-Path-Collision`)    |
| `E-MOD-1105` | Module-path component is a reserved keyword (`WF-Module-Path-Reserved`)                           |
| `E-MOD-1106` | Module-path component is not a valid identifier (`WF-Module-Path-Ident-Err`)                      |
| `W-MOD-1101` | Warning: potential module-path collision on a case-insensitive filesystem                         |

### The `uv` Command Line

The compiler driver is invoked as `uv <command>`. The two project commands relevant to this chapter are **build** and **test**.

- **Build** loads the project (§3.3), runs the output pipeline (§3.6), and links/archives the primary artifact. The target profile is taken from the CLI override if present, else `[toolchain].target_profile`; with neither, the invocation fails (`E-PRJ-0112`).
- **Test** (`uv test [target]`) discovers `#test` procedures under each assembly's `<AssemblyName>::Tests` subtree (`TestsPrefix(A) = A.name :: Tests`), generates an ephemeral harness in each test-bearing assembly's output directory, compiles `AssemblyProject(P, A)` with that harness as entrypoint, and runs the selected tests in deterministic order. The optional positional `target` selects scope:

  ```text
  ResolveTestTarget(P, ⊥)  = AllTests
  ResolveTestTarget(P, s)  = SourceFileTests(p)   if s is an existing file
                             AllTests              if s is the project root directory
                             DirectoryTests(p)     if s is an existing directory ≠ root
                             AssemblyTests(A)      if s (not a host path) names an assembly
                             ModuleTests(q)        if s parses as a module path of a known module
                             ⇑ Test-Target-Err     otherwise          (E-TST-0108)
  ```

  Test discovery order is module path, then file order, then declaration span, then fully-qualified procedure symbol — that fully-qualified path is the stable test identity (a `name: "..."` argument is only a display label).

When a project has multiple assemblies, name the assembly to select it (the `target` of project load), since automatic selection only succeeds for a single assembly or a unique executable (§3.3).

> Note on terminology: the specification names the driver `uv` and refers to the profile selector as the **CLI target-profile override** (the highest-precedence input to `SelectedTargetProfile`). However a given front-end spells the flag, the selector must supply a value in `TargetProfile`; there is no host-platform inference.

### Idioms & Best Practices

- **One directory, one module; split by directory, not by file.** Keep a module's files in one directory and let multiple `.uv` files share the module (the compilation unit). When a module grows past roughly 400 lines or mixes responsibilities, introduce a *subdirectory* (a submodule), not just another sibling file. See the **Modules, Imports & Visibility** chapter for the surface rules.
- **PascalCase everything that becomes a path component.** Assemblies, directories, and files are `PascalCase`; directory names become module-path identifiers, so they must be valid identifiers and never reserved keywords. Preserve established acronyms (`FrameGraph`, `D3D12Device`); do not normalize them to `Framegraph`.
- **Use `Main.uv` for the executable root file, but keep the entry procedure `main`.** The file name is project-controlled and conventional; the entry symbol is language-mandated.
- **Keep `Api.uv` thin.** Reserve it for a module's facade/export surface; it coordinates exports rather than accumulating logic.
- **Declare visibility explicitly** on every item, including `import` and `using` declarations — visibility is part of the API contract (see the AGENTS style guide).
- **Document public surfaces.** Every public module gets a `//!` module-doc; every public type, procedure, method, transition, and exported constant gets `///` docs. Because a module spans all files in its directory, any one file may carry the `//!` module documentation.
- **Pin the target profile.** Set `[toolchain].target_profile` in the manifest for reproducible local builds, and use the CLI override only to retarget. Never rely on host inference — it is forbidden, and an unset profile is `E-PRJ-0112`.
- **Choose `kind` deliberately.** Use `executable` for the program, `library` (with explicit `link_kind`) for an independently linked artifact, and `dependency` for shared code that should fold into its importer rather than link standalone.
- **Keep `root` and `out_dir` relative and inside the project.** Both must be relative and resolve under `R`; do not point them outside the project tree.

### Pitfalls & Diagnostics

- **Expecting per-file modules.** Adding `Helper.uv` next to `Main.uv` does *not* create a `Helper` module — both files are the *same* module (the directory's). To get a new module, add a subdirectory. (§3.5)
- **Missing or malformed manifest.** No `Ultraviolet.toml` at the root is `E-PRJ-0101`; invalid TOML is `E-PRJ-0102`. There is no single-file fallback — the manifest is mandatory.
- **Stray top-level or assembly keys.** Only `assembly`, `toolchain`, `build` are valid top-level keys; only `name`, `kind`, `root`, `out_dir`, `emit_ir`, `link_kind` are valid assembly keys. A typo'd key is `E-PRJ-0104`, not a silently ignored setting.
- **`link_kind` on the wrong kind.** `link_kind` is valid *only* for `library`; a bad value on a library is `E-PRJ-0207`, while placing it on an `executable` or `dependency` at all is `E-PRJ-0208`.
- **Ambiguous overlapping roots.** Giving two assemblies the same `root`, or roots whose ownership of a module directory is not uniquely the deepest, is `E-PRJ-0206`. Make one root nest cleanly inside the other.
- **Case-fold module collisions.** Two directories whose paths fold equal (`Frame` vs `frame`) collide during discovery as `E-MOD-1104` (`Disc-Collision`) and additionally warn with `W-MOD-1101` on case-insensitive hosts. Keep directory casing distinct and consistent.
- **Reserved-word or invalid directory names.** A directory named `type`, `loop`, `class`, etc. makes an invalid module-path component, `E-MOD-1105` (`WF-Module-Path-Reserved`); a directory name that is not a valid identifier is `E-MOD-1106` (`WF-Module-Path-Ident-Err`).
- **Forgetting the target profile.** Omitting both the CLI override and `[toolchain].target_profile` is `E-PRJ-0112` — the build cannot proceed without a profile.
- **No unique executable to select.** With several assemblies and no named target, selection succeeds only if exactly one is an `executable`; otherwise `E-PRJ-0205`. Pass the assembly name to disambiguate.
- **Toolchain tools not found.** A missing linker/archiver is `Out-Link-NotFound`; a missing or version-mismatched `llvm-as` blocks `emit_ir = bc`. Set `[toolchain].llvm_bin` to a directory containing the matching toolchain, or ensure it is on `PATH`. (§3.7)
- **Runtime library problems.** An unresolvable runtime library is `Out-Link-Runtime-Missing`; one lacking required symbols is `Out-Link-Runtime-Incompatible`. Point `[toolchain].runtime_lib` at the correct runtime for the selected profile.
- **Invalid `main`.** For an executable: zero `main` is `E-MOD-2434`, more than one is `E-MOD-2430`, a generic `main` is `E-MOD-2432`, and any signature other than `public procedure main(context: Context) -> i32` (parameter mode omitted or `move`) is `E-MOD-2431`.
- **Unknown `uv test` target.** A positional `target` that is neither a host path, an assembly name, nor a known module path is `E-TST-0108` (`Test-Target-Err`).
