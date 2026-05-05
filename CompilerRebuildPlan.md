# Ultraviolet Compiler Rebuild Plan

Date: 2026-05-05

This plan lives in the Ultraviolet repository and is the execution contract for rebuilding the full Ultraviolet compiler in Ultraviolet, bootstrapped by the existing Cursive compiler. It is intentionally concrete: implementation must migrate one audited object at a time, but the target architecture is the complete compiler, not a minimal vertical slice.

The authoritative language contract is `SPECIFICATION.md`. The current obligation ledger used by this plan is `Docs/Audit/ULTRAVIOLET_OBLIGATIONS.csv`; the scaffold pass must preserve ledger contents while normalizing final audit/tool names to the PascalCase paths defined below.

## Non-Negotiable Constraints

- `SPECIFICATION.md` is the source of truth. Do not edit it as part of the rebuild unless the user explicitly approves the spec change.
- The rebuild is a full self-hosting compiler implementation. Do not create a partial/minimal compiler architecture and do not mechanically rename C++ files into Ultraviolet.
- Every target directory is PascalCase. Every Ultraviolet source file is `PascalCase.uv` except externally mandated names inside source text, ABI symbols, serialized keys, and the public `uv` command name.
- The Cursive compiler is Stage 0 only. It may be patched only for bootstrap blockers or confirmed spec-conformance defects needed to compile Ultraviolet.
- No shim, adapter, duplicate implementation, fallback path, test-only branch, or compatibility layer is allowed unless the specification explicitly defines that boundary.
- No new validation-script layer is allowed. Keep the obligation extraction/ledger tooling only; conformance evidence must come from compiler tests, fixtures, traces, and bootstrap outputs.
- Windows `x86_64-win64` is the first bootstrap target. The implementation must not infer a target profile from the host platform.

## Target Repository Shape

The scaffold pass must produce the exact file tree below. Do not add compiler or runtime source files outside this tree unless this plan is updated first. The public command remains `uv`; that is a command-name exception, not a directory-name exception. Directories use PascalCase and acronym-preserving names such as `IR`, `ABI`, `LLVM`, and `IO`.

### Exact File Tree

```text
Ultraviolet.toml
CompilerRebuildPlan.md
AGENTS.md
Compiler/
  Api.uv
  Foundation/
    BehaviorModel.uv
    ConformanceModel.uv
    Hashing.uv
    Identifiers.uv
    Paths.uv
    SourceRegistry.uv
    Spans.uv
    SpecificationTrace.uv
    Terminal.uv
    Unicode.uv
  Diagnostics/
    Codes.uv
    Diagnostic.uv
    Emission.uv
    Ordering.uv
    Recovery.uv
    Rendering.uv
    Sources.uv
  Project/
    AssemblyGraph.uv
    AssemblyLoader.uv
    AssemblySelection.uv
    BuildConfig.uv
    DeterministicOrder.uv
    Manifest.uv
    ManifestValidation.uv
    ModuleDiscovery.uv
    OutputArtifacts.uv
    PathResolution.uv
    ProjectModel.uv
    RootDiscovery.uv
    TargetProfile.uv
    ToolchainConfig.uv
  Source/
    Text/
      LogicalLines.uv
      SourceLoading.uv
      UnicodeSecurity.uv
      UTF8.uv
    Lexer/
      CharacterClasses.uv
      Comments.uv
      Identifiers.uv
      Keywords.uv
      Literals.uv
      Operators.uv
      Scanner.uv
      Tokenize.uv
      Tokens.uv
    Ast/
      AstDump.uv
      AstNodes.uv
      Attributes.uv
      Expressions.uv
      Items.uv
      Modules.uv
      Patterns.uv
      Statements.uv
      Types.uv
    Parser/
      AttributeParser.uv
      DocComments.uv
      ExpressionParser.uv
      ItemParser.uv
      ListParser.uv
      ModuleParser.uv
      ParserState.uv
      PathParser.uv
      PatternParser.uv
      PermissionParser.uv
      Recovery.uv
      Terminators.uv
      TokenCursor.uv
      TypeParser.uv
  Comptime/
    DependencyOrder.uv
    Derive.uv
    Eval.uv
    Files.uv
    Hygiene.uv
    Pass.uv
    Quote.uv
    Reflect.uv
    Rewrite.uv
  Driver/
    BuildCommand.uv
    CheckCommand.uv
    CleanCommand.uv
    CLI.uv
    Commands.uv
    ConformanceTrace.uv
    CrashReporting.uv
    Fingerprints.uv
    Incremental.uv
    InitCommand.uv
    Options.uv
    Pipeline.uv
    RunCommand.uv
    Version.uv
  Resolve/
    Disambiguation.uv
    Imports.uv
    Lookup.uv
    Modules.uv
    NameIntroduction.uv
    Qualified.uv
    ResolveContracts.uv
    ResolveExpressions.uv
    ResolveItems.uv
    ResolvePatterns.uv
    ResolveTypes.uv
    Scopes.uv
    Using.uv
    Visibility.uv
  Semantics/
    RuntimeModel/
      AbstractMachine.uv
      ObservableBehavior.uv
      SequencePoints.uv
    Capabilities/
      AuthorityModel.uv
      CallGraph.uv
      ContextCapabilities.uv
      FileSystemCapabilities.uv
      HeapCapabilities.uv
      NetworkCapabilities.uv
      SystemCapabilities.uv
    Attributes/
      AttributeTargets.uv
      AttributeValidation.uv
      DiagnosticsMetadata.uv
      LayoutAttributes.uv
      OptimizationAttributes.uv
    Types/
      Aliases.uv
      Arrays.uv
      CapabilityClasses.uv
      Classes.uv
      Closures.uv
      DynamicObjects.uv
      Enums.uv
      FunctionTypes.uv
      Generics.uv
      ModalTypes.uv
      OpaqueTypes.uv
      Pointers.uv
      PrimitiveTypes.uv
      Ranges.uv
      Records.uv
      Refinements.uv
      Slices.uv
      StringsBytes.uv
      Tuples.uv
      TypeEquivalence.uv
      TypeInference.uv
      TypeModel.uv
      TypeWellFormedness.uv
      Unions.uv
    Permissions/
      Admissibility.uv
      AliasExclusivity.uv
      BindingState.uv
      DynamicContext.uv
      PermissionModel.uv
    Memory/
      BorrowBinding.uv
      DropSemantics.uv
      InitializationState.uv
      Provenance.uv
      Regions.uv
      SafePointers.uv
    Declarations/
      Contracts.uv
      Invariants.uv
      Methods.uv
      Procedures.uv
      TypeDeclarations.uv
    Expressions/
      Calls.uv
      Closures.uv
      ExpressionCheck.uv
      Literals.uv
      Operators.uv
      Places.uv
    Statements/
      Blocks.uv
      ControlFlow.uv
      Defer.uv
      LetVar.uv
      StatementCheck.uv
    Patterns/
      Exhaustiveness.uv
      PatternCheck.uv
      PatternModel.uv
    Keys/
      KeyAcquisition.uv
      KeyConflict.uv
      KeyLifetimes.uv
      KeyModel.uv
    Parallelism/
      Dispatch.uv
      Parallel.uv
      Spawn.uv
      StructuredConcurrency.uv
    Async/
      AsyncModel.uv
      Await.uv
      Future.uv
      Race.uv
      Sync.uv
    FFI/
      ExportValidation.uv
      ForeignBoundary.uv
      HostedExports.uv
      UnsafeSurface.uv
    Initialization/
      Cleanup.uv
      ModuleLifecycle.uv
      StaticInitialization.uv
  Lowering/
    AttributeLowering.uv
    BindingLowering.uv
    CheckLowering.uv
    CleanupLowering.uv
    ExpressionLowering.uv
    InitializationLowering.uv
    LoweringContext.uv
    ModuleLowering.uv
    PatternLowering.uv
    PermissionLowering.uv
    StatementLowering.uv
  Backend/
    BackendDiagnostics.uv
    BackendPipeline.uv
    IR/
      Globals.uv
      IRBuilder.uv
      IRDump.uv
      IRModel.uv
      Literals.uv
      Poison.uv
      RuntimeSymbols.uv
      VTables.uv
    ABI/
      ABICalls.uv
      ABIModel.uv
      ABIParameters.uv
      ABIReturns.uv
      ABITypes.uv
      CallingConventions.uv
      ForeignABI.uv
      HostedABI.uv
    LLVM/
      LLVMAttributes.uv
      LLVMCalls.uv
      LLVMEmit.uv
      LLVMModule.uv
      LLVMPanic.uv
      LLVMSafety.uv
      LLVMTarget.uv
      LLVMTools.uv
      LLVMTypes.uv
    Link/
      ArchivePlan.uv
      ArtifactFinalize.uv
      ExternLibraries.uv
      LinkPlan.uv
      RuntimeResolution.uv
      ToolInvocation.uv
  LanguageService/
    DiagnosticsAdapter.uv
    DocumentStore.uv
    JSONRPC.uv
    LineIndex.uv
    LSPProtocol.uv
    Navigation.uv
    Rename.uv
    SemanticTokens.uv
    Server.uv
    Snapshot.uv
    SymbolIndex.uv
Runtime/
  Api.uv
  Host/
    ABI.uv
    PanicBoundary.uv
    Platform.uv
    Session.uv
    Startup.uv
  Context/
    Capabilities.uv
    Context.uv
    FileSystemContext.uv
    HeapContext.uv
    NetworkContext.uv
    SystemContext.uv
  Memory/
    Allocation.uv
    AsyncStorage.uv
    BindingStore.uv
    Bytes.uv
    Frames.uv
    Regions.uv
    Strings.uv
    ValueModel.uv
  Concurrency/
    Cancellation.uv
    Dispatch.uv
    Keys.uv
    Parallel.uv
    Reactor.uv
    Spawn.uv
  IO/
    Directory.uv
    File.uv
    FileSystem.uv
    Paths.uv
  Network/
    Network.uv
    Restrictions.uv
  System/
    DynamicLibrary.uv
    Environment.uv
    Process.uv
Tools/
  Uv/
    Main.uv
  ExtractObligationLedger.py
Tests/
  Bootstrap/
    Stage0.uv
    Stage1.uv
    Stage2.uv
  Conformance/
    Accept.uv
    Bootstrap.uv
    Diagnostic.uv
    Phase.uv
    Reject.uv
    Target.uv
  Regression/
    CompilerBugs.uv
Docs/
  Audit/
    MigrationLedger.md
    UltravioletObligations.csv
  Internal/
    UltravioletSpecification.obligations.md
```

`Ultraviolet.toml` must define exactly these initial assemblies:

- `UltravioletRT`: `kind = "library"`, `link_kind = "static"`, `root = "Runtime"`; produces `UltravioletRT.lib` on `x86_64-win64`.
- `UltravioletCompiler`: `kind = "library"`, `link_kind = "static"`, `root = "Compiler"`; contains all compiler logic except the CLI entrypoint.
- `uv`: `kind = "executable"`, `root = "Tools/Uv"`; this is the only lowercase assembly-name exception because it owns the public command artifact.

Only `uv` is executable so default assembly selection is unambiguous. If the implementation later adds executable test tools, they must be marked and built by explicit `--assembly` only.

### Module Obligation Responsibility Matrix

This matrix is the exact primary ownership map for the rebuild. For every module below, each listed obligation owner means the module is responsible for satisfying every exact obligation ID listed for that owner in Appendix A. No obligation owner from the current ledger is unassigned or assigned to more than one primary module.

### `Compiler/Foundation`

Shared compiler primitives: conformance vocabulary, behavior classes, identifiers, paths, spans, Unicode helpers, and spec tracing.

Obligation owners:
- `conformance.behavior-types` (8 total, 8 required)
- `conformance.check-kinds` (4 total, 4 required)
- `conformance.constructs` (11 total, 11 required)
- `conformance.definitions` (3 total, 3 required)
- `conformance.document-conventions` (5 total, 5 required)
- `conformance.error-recovery` (6 total, 5 required, 1 recommended)
- `conformance.normative-references` (3 total, 2 required, 1 informative)
- `conformance.outside-conformance` (2 total, 2 required)
- `conformance.rejection` (1 total, 1 required)
- `conformance.undefinedness` (5 total, 5 required)
- `front-matter.document-organization` (1 total, 1 required)
- `front-matter.feature-section-template` (1 total, 1 required)
- `front-matter.language-design-contract` (1 total, 0 required, 1 recommended)

### `Compiler/Diagnostics`

Diagnostic codes, source association, ordering, recovery policy, emission, and rendering.

Obligation owners:
- `diagnostics.attributes` (2 total, 2 required)
- `diagnostics.attributes.layout` (1 total, 1 required)
- `diagnostics.attributes.metadata` (1 total, 1 required)
- `diagnostics.attributes.optimization` (1 total, 1 required)
- `diagnostics.code-selection` (5 total, 5 required)
- `diagnostics.emission` (6 total, 6 required)
- `diagnostics.memory` (1 total, 1 required)
- `diagnostics.name-resolution` (1 total, 1 required)
- `diagnostics.no-source-span` (1 total, 1 required)
- `diagnostics.ordering` (1 total, 1 required)
- `diagnostics.parsing` (4 total, 4 required)
- `diagnostics.permissions` (2 total, 2 required)
- `diagnostics.project` (1 total, 1 required)
- `diagnostics.records` (5 total, 5 required)
- `diagnostics.rendering` (10 total, 10 required)
- `diagnostics.source-lexical` (1 total, 1 required)
- `diagnostics.source-loading` (9 total, 9 required)
- `diagnostics.source-spans` (7 total, 7 required)
- `diagnostics.token-spans` (6 total, 6 required)
- `diagnostics.types` (2 total, 1 required)

### `Compiler/Project`

Project records, manifests, assembly loading, module discovery, target profiles, and output artifact naming.

Obligation owners:
- `conformance.target-abi` (7 total, 7 required)
- `project.assembly-graph` (16 total, 16 required)
- `project.assembly-loader` (5 total, 5 required)
- `project.assembly-ownership` (5 total, 5 required)
- `project.assembly-selection` (4 total, 4 required)
- `project.build-config` (4 total, 4 required)
- `project.context` (1 total, 1 required)
- `project.core-records` (3 total, 3 required)
- `project.deterministic-ordering` (10 total, 10 required)
- `project.loader-state-machine` (14 total, 14 required)
- `project.manifest-loader` (3 total, 3 required)
- `project.manifest-parser` (4 total, 4 required)
- `project.manifest-schema` (12 total, 12 required)
- `project.manifest-validation` (31 total, 31 required)
- `project.module-discovery` (7 total, 7 required)
- `project.output-artifacts` (43 total, 43 required)
- `project.path-resolution` (20 total, 20 required)
- `project.root-discovery` (5 total, 5 required)
- `project.source-roots` (3 total, 3 required)
- `project.toolchain-config` (6 total, 6 required)
- `project.validation-scope` (1 total, 1 required)

### `Compiler/Source/Text`

UTF-8, BOM, source loading, Unicode normalization, logical lines, and statement terminator inputs.

Obligation owners:
- `lexer.source-loading` (35 total, 35 required)
- `lexer.statement-termination` (11 total, 11 required)
- `lexer.unicode` (13 total, 13 required)
- `lexer.unicode-security` (2 total, 2 required)

### `Compiler/Source/Lexer`

Token records and lexical scanning for comments, literals, identifiers, keywords, operators, security, and tokenization.

Obligation owners:
- `lexer.character-classes` (8 total, 8 required)
- `lexer.comments` (11 total, 11 required)
- `lexer.identifiers` (6 total, 6 required)
- `lexer.literals` (53 total, 53 required)
- `lexer.maximal-munch` (9 total, 9 required)
- `lexer.operators` (3 total, 3 required)
- `lexer.records` (9 total, 9 required)
- `lexer.reserved-lexemes` (6 total, 6 required)
- `lexer.security` (11 total, 11 required)
- `lexer.tokenization` (18 total, 18 required)
- `lexer.tokens` (5 total, 5 required)

### `Compiler/Source/Ast`

AST node definitions, module/file AST forms, attributes, and AST inspection/dump helpers.

Obligation owners:
- `parser.ast` (10 total, 10 required)

### `Compiler/Source/Parser`

Parser state, token consumption, grammar helpers, lists, terminators, items, attributes, types, permissions, FFI shells, and module aggregation.

Obligation owners:
- `parser` (7 total, 7 required)
- `parser.attributes` (42 total, 42 required)
- `parser.attributes.layout` (3 total, 3 required)
- `parser.attributes.metadata` (4 total, 4 required)
- `parser.attributes.optimization` (3 total, 3 required)
- `parser.doc-comments` (5 total, 5 required)
- `parser.ffi` (1 total, 1 required)
- `parser.file` (2 total, 2 required)
- `parser.items` (2 total, 2 required)
- `parser.list-parsing` (11 total, 11 required)
- `parser.modules` (58 total, 58 required)
- `parser.permissions` (10 total, 10 required)
- `parser.phase` (5 total, 5 required)
- `parser.recovery` (12 total, 12 required)
- `parser.shared-grammar` (6 total, 6 required)
- `parser.shared-helpers` (17 total, 17 required)
- `parser.state` (11 total, 11 required)
- `parser.terminators` (8 total, 8 required)
- `parser.token-consumption` (6 total, 6 required)
- `parser.types` (16 total, 16 required)
- `spec.grammar` (18 total, 18 required)

### `Compiler/Comptime`

Compile-time dependency order, evaluation, files, derive, reflection, quotation, hygiene, and rewrites.

Obligation owners:
- `spec.comptime` (181 total, 181 required)

### `Compiler/Driver`

CLI commands, compilation pipeline ordering, conformance traces, incremental fingerprints, crash reporting, and version surface.

Obligation owners:
- `conformance.phase-ordering` (8 total, 8 required)
- `conformance.translation-phases` (6 total, 6 required)
- `project.command-output` (6 total, 6 required)

### `Compiler/Resolve`

Scopes, name introduction, imports, using declarations, visibility, lookup, qualified resolution, and disambiguation.

Obligation owners:
- `checker.modules` (209 total, 209 required)
- `checker.name-resolution` (307 total, 307 required)
- `checker.visibility` (15 total, 15 required)

### `Compiler/Semantics/RuntimeModel`

Static model of observable behavior, abstract-machine sequence points, and runtime state consumed by analysis and lowering.

Obligation owners:
- `abstract-machine.observable-behavior` (5 total, 5 required)
- `abstract-machine.sequence-points` (3 total, 3 required)

### `Compiler/Semantics/Capabilities`

Authority model, capability roots, no-ambient-authority checks, attenuation, and capability call-graph facts.

Obligation owners:
- `authority.attenuation` (6 total, 6 required)
- `authority.capabilities` (8 total, 7 required)
- `authority.model` (1 total, 1 required)
- `authority.no-ambient-authority` (5 total, 5 required)

### `Compiler/Semantics/Attributes`

Attribute target validation and static semantics for layout, metadata, diagnostics, and optimization attributes.

Obligation owners:
- `checker.attributes` (16 total, 16 required)
- `checker.attributes.layout` (10 total, 10 required)
- `checker.attributes.metadata` (23 total, 23 required)
- `checker.attributes.optimization` (2 total, 2 required)

### `Compiler/Semantics/Types`

Type model and static semantics for primitive, aggregate, modal, class, generic, pointer, function, closure, alias, opaque, and refinement forms.

Obligation owners:
- `checker.types` (283 total, 181 required)
- `checker.types.arrays` (34 total, 34 required)
- `checker.types.primitive` (13 total, 13 required)
- `checker.types.ranges` (55 total, 55 required)
- `checker.types.slices` (28 total, 28 required)
- `checker.types.tuples` (41 total, 41 required)

### `Compiler/Semantics/Permissions`

Permission forms, binding activity state, admissibility, alias/exclusivity rules, and dynamic-context facts.

Obligation owners:
- `checker.binding-state` (72 total, 72 required)
- `checker.permission-state` (18 total, 18 required)
- `checker.permissions` (32 total, 32 required)

### `Compiler/Semantics/Memory`

Provenance, region checks, borrow binding, safe pointer checks, initialization state, and drop-state facts.

Obligation owners:
- `checker.provenance` (51 total, 51 required)
- `checker.regions` (12 total, 12 required)

### `Compiler/Semantics/Declarations`

Type declarations, procedures, methods, contracts, invariants, modal declarations, abstraction, and polymorphism surfaces.

Obligation owners:
- `spec.abstraction-polymorphism` (328 total, 327 required)
- `spec.modal-special` (386 total, 386 required)
- `spec.procedures-contracts` (283 total, 283 required)

### `Compiler/Semantics/Expressions`

Expression typing and semantics for literals, calls, operators, closures, places, and expression-level effects.

Obligation owners:
- `spec.expressions` (478 total, 475 required)

### `Compiler/Semantics/Statements`

Statement typing and semantics for blocks, bindings, control flow, loops, defer, and sequencing.

Obligation owners:
- `spec.statements` (260 total, 260 required)

### `Compiler/Semantics/Patterns`

Pattern typing, exhaustiveness, binding introduction, and case coverage.

Obligation owners:
- `spec.patterns` (161 total, 161 required)

### `Compiler/Semantics/Keys`

Shared-key acquisition, release, conflict analysis, lifetimes, and dynamic fallback eligibility.

Obligation owners:
- `spec.key-system` (185 total, 175 required)

### `Compiler/Semantics/Parallelism`

Structured parallelism, spawn, dispatch, execution domains, and concurrency static checks.

Obligation owners:
- `spec.structured-parallelism` (181 total, 180 required)

### `Compiler/Semantics/Async`

Async modal values, await, race, sync, futures, and suspension/resumption checks.

Obligation owners:
- `spec.async` (254 total, 253 required)

### `Compiler/Semantics/FFI`

Foreign ABI validation, unsafe and hosted export surfaces, FFI type safety, and boundary restrictions.

Obligation owners:
- `authority.unsafe-ffi` (1 total, 1 required)
- `checker.ffi` (1 total, 1 required)
- `spec.ffi` (203 total, 203 required)

### `Compiler/Semantics/Initialization`

Module lifecycle, static initialization, cleanup, deinitialization, and unwind responsibilities before lowering.

Obligation owners:
- `spec.cleanup` (56 total, 56 required)
- `spec.initialization` (102 total, 102 required)

### `Compiler/Lowering`

Canonical lowering from accepted semantics into compiler IR, including attributes, permissions, checks, cleanup, bindings, expressions, statements, and patterns.

Obligation owners:
- `lowering.attributes` (2 total, 2 required)
- `lowering.attributes.layout` (1 total, 1 required)
- `lowering.attributes.metadata` (1 total, 1 required)
- `lowering.attributes.optimization` (1 total, 1 required)
- `lowering.permissions` (3 total, 3 required)
- `spec.lowering` (158 total, 158 required)

### `Compiler/Backend`

Backend-level ownership for target-independent backend rules, backend diagnostics, and final backend pipeline coordination.

Obligation owners:
- `spec.backend` (190 total, 190 required)

### `Compiler/Backend/IR`

IR model, global/static symbols, runtime symbols, literal data, vtables, poison state, and IR dump/build helpers.

Obligation owners:
- `codegen` (51 total, 51 required)
- `spec.runtime-interface` (64 total, 64 required)
- `spec.symbols` (51 total, 51 required)

### `Compiler/Backend/LLVM`

LLVM target constants, LLVM type/attribute/call lowering, LLVM emission, panic/safety lowering, and LLVM tool integration.

Obligation owners:
- `backend.llvm-codegen` (4 total, 4 required)
- `backend.llvm-target` (3 total, 3 required)

### `Compiler/Backend/Link`

Output pipeline, linker/archive plans, runtime library resolution, external library materialization, and tool invocation.

Obligation owners:
- `project.linker` (31 total, 31 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.tool-resolution` (7 total, 7 required)

### `Runtime/Host`

Runtime host ABI, hosted sessions, primitive dispatch, panic boundaries, startup, and runtime attribute behavior.

Obligation owners:
- `runtime` (23 total, 23 required)
- `runtime.attributes` (2 total, 2 required)
- `runtime.attributes.layout` (1 total, 1 required)
- `runtime.attributes.metadata` (1 total, 1 required)
- `runtime.attributes.optimization` (1 total, 1 required)
- `runtime.host-primitives` (7 total, 7 required)
- `runtime.primitive-method-application` (35 total, 35 required)

### `Runtime/Memory`

Runtime binding store, permission state, regions, frames, strings, bytes, async storage, and value model.

Obligation owners:
- `runtime.binding-store` (35 total, 35 required)
- `runtime.permissions` (2 total, 2 required)
- `runtime.regions` (45 total, 45 required)
- `runtime.value-model` (17 total, 17 required)

### `Runtime/IO`

Filesystem, file, directory, and path primitives.

Obligation owners:
- `runtime.filesystem-primitives` (56 total, 56 required)

### `Runtime/Network`

Network primitive relations and restrictions.

Obligation owners:
- `runtime.network-primitives` (6 total, 6 required)

### `Runtime/System`

System/process/environment/dynamic-library primitive relations.

Obligation owners:
- `runtime.system-primitives` (8 total, 8 required)

## Bootstrap Ladder

Stage 0 compiler path for the current workstation: `/mnt/c/Dev/Cursive/cursive/build/windows/Release/Cursive.exe`.

Required Stage 0 commands use Windows target selection explicitly:

```sh
"/mnt/c/Dev/Cursive/cursive/build/windows/Release/Cursive.exe" --bootstrap-ultraviolet --target-profile x86_64-win64 --check /mnt/c/Dev/Ultraviolet
"/mnt/c/Dev/Cursive/cursive/build/windows/Release/Cursive.exe" --bootstrap-ultraviolet --target-profile x86_64-win64 build /mnt/c/Dev/Ultraviolet --assembly UltravioletRT
"/mnt/c/Dev/Cursive/cursive/build/windows/Release/Cursive.exe" --bootstrap-ultraviolet --target-profile x86_64-win64 build /mnt/c/Dev/Ultraviolet --assembly UltravioletCompiler
"/mnt/c/Dev/Cursive/cursive/build/windows/Release/Cursive.exe" --bootstrap-ultraviolet --target-profile x86_64-win64 build /mnt/c/Dev/Ultraviolet --assembly uv
```

The self-host ladder is:

1. Stage 0: Cursive compiles `UltravioletRT`, `UltravioletCompiler`, and `uv` from Ultraviolet source.
2. Stage 1: Stage 0 `uv.exe` compiles the same assemblies.
3. Stage 2: Stage 1 `uv.exe` compiles the same assemblies again.
4. Completion requires matching diagnostics, matching conformance traces, stable normalized IR, and stable artifact fingerprints between Stage 1 and Stage 2.

## Per-File Migration Protocol

Each migrated source object must go through this exact gate before it is accepted:

1. Identify the C++ source object(s), current tests, and current call sites.
2. Identify the canonical target Ultraviolet module and file; do not preserve the old numbered C++ phase layout.
3. Map the object to obligation owners and exact obligation IDs from Appendix A.
4. Read the corresponding `SPECIFICATION.md` section before writing code.
5. Implement the general rule in the canonical module; delete or avoid any duplicate behavior path.
6. Add conformance fixtures for valid examples, invalid examples, diagnostics, phase ordering, and relevant target-profile behavior.
7. Run Stage 0 `--check` for the affected assembly and the targeted conformance fixture set.
8. Record in `Docs/Audit/MigrationLedger.md`: source object, target file, obligation IDs, tests, command output summary, and any C++ bootstrap bug found.

A file is not migrated if it merely compiles. It is migrated only when its mapped required obligations are covered by tests or an explicit, reviewed non-applicability note.

## Workstreams And Required Obligation Owners

Each workstream lists the exact obligation owners it must discharge. Appendix A lists the exact obligation IDs under each owner.

### W0. Repository Scaffold And Style Normalization

Create the final PascalCase repository structure, normalize existing lowercase Docs/Tools material, and introduce the Stage 0 manifest without compiler logic.

Obligation owners:
- `front-matter.language-design-contract` (1 total, 0 required, 1 recommended)
- `conformance.document-conventions` (5 total, 5 required)
- `project.core-records` (3 total, 3 required)
- `project.manifest-schema` (12 total, 12 required)
- `project.manifest-validation` (31 total, 31 required)
- `project.source-roots` (3 total, 3 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W1. Bootstrap Contract

Lock the Stage 0 Cursive bootstrap invocation, Windows target profile, runtime library name, tool resolution, and no-host-inference policy.

Obligation owners:
- `conformance.translation-phases` (6 total, 6 required)
- `conformance.phase-ordering` (8 total, 8 required)
- `conformance.target-abi` (7 total, 7 required)
- `project.toolchain-config` (6 total, 6 required)
- `project.tool-resolution` (7 total, 7 required)
- `project.output-artifacts` (43 total, 43 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W2. Foundation And Diagnostics

Rebuild spans, source positions, diagnostic records, deterministic emission, rendering, and no-source diagnostics before frontend migration.

Obligation owners:
- `diagnostics.source-spans` (7 total, 7 required)
- `diagnostics.token-spans` (6 total, 6 required)
- `diagnostics.records` (5 total, 5 required)
- `diagnostics.emission` (6 total, 6 required)
- `diagnostics.code-selection` (5 total, 5 required)
- `diagnostics.ordering` (1 total, 1 required)
- `diagnostics.rendering` (10 total, 10 required)
- `diagnostics.no-source-span` (1 total, 1 required)
- `diagnostics.source-loading` (9 total, 9 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W3. Project Model And Output Planning

Rebuild manifest parsing, assembly selection, module discovery, output paths, link plans, and tool/runtime resolution exactly as the spec defines them.

Obligation owners:
- `project.manifest-parser` (4 total, 4 required)
- `project.root-discovery` (5 total, 5 required)
- `project.loader-state-machine` (14 total, 14 required)
- `project.assembly-loader` (5 total, 5 required)
- `project.assembly-selection` (4 total, 4 required)
- `project.assembly-ownership` (5 total, 5 required)
- `project.assembly-graph` (16 total, 16 required)
- `project.deterministic-ordering` (10 total, 10 required)
- `project.module-discovery` (7 total, 7 required)
- `project.command-output` (6 total, 6 required)
- `project.output-artifacts` (43 total, 43 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.linker` (31 total, 31 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W4. Source Text And Lexer

Rebuild UTF-8/BOM/newline handling, NFC-sensitive identifier rules, token records, comments, whitespace, literals, identifiers, operators, security checks, and tokenization.

Obligation owners:
- `lexer.source-loading` (35 total, 35 required)
- `lexer.unicode` (13 total, 13 required)
- `lexer.unicode-security` (2 total, 2 required)
- `lexer.statement-termination` (11 total, 11 required)
- `lexer.character-classes` (8 total, 8 required)
- `lexer.reserved-lexemes` (6 total, 6 required)
- `lexer.tokens` (5 total, 5 required)
- `lexer.comments` (11 total, 11 required)
- `lexer.literals` (53 total, 53 required)
- `lexer.identifiers` (6 total, 6 required)
- `lexer.operators` (3 total, 3 required)
- `lexer.maximal-munch` (9 total, 9 required)
- `lexer.security` (11 total, 11 required)
- `lexer.tokenization` (18 total, 18 required)
- `spec.grammar` (18 total, 18 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W5. Parser, AST, Attributes, And Module Aggregation

Rebuild parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, and file-to-module aggregation.

Obligation owners:
- `parser.phase` (5 total, 5 required)
- `parser.ast` (10 total, 10 required)
- `parser.state` (11 total, 11 required)
- `parser.shared-grammar` (6 total, 6 required)
- `parser.shared-helpers` (17 total, 17 required)
- `parser.token-consumption` (6 total, 6 required)
- `parser.list-parsing` (11 total, 11 required)
- `parser.terminators` (8 total, 8 required)
- `parser.file` (2 total, 2 required)
- `parser.items` (2 total, 2 required)
- `parser.doc-comments` (5 total, 5 required)
- `parser.recovery` (12 total, 12 required)
- `parser.attributes` (42 total, 42 required)
- `parser.attributes.layout` (3 total, 3 required)
- `parser.attributes.metadata` (4 total, 4 required)
- `parser.attributes.optimization` (3 total, 3 required)
- `parser.modules` (58 total, 58 required)
- `parser.ffi` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W6. Authority And Runtime Model

Rebuild capability roots, no-ambient-authority guarantees, attenuation, observable behavior, sequence points, host primitives, binding store, permissions, regions, and runtime values.

Obligation owners:
- `authority.model` (1 total, 1 required)
- `authority.capabilities` (8 total, 7 required)
- `authority.no-ambient-authority` (5 total, 5 required)
- `authority.attenuation` (6 total, 6 required)
- `authority.unsafe-ffi` (1 total, 1 required)
- `abstract-machine.observable-behavior` (5 total, 5 required)
- `abstract-machine.sequence-points` (3 total, 3 required)
- `runtime.host-primitives` (7 total, 7 required)
- `runtime.filesystem-primitives` (56 total, 56 required)
- `runtime.system-primitives` (8 total, 8 required)
- `runtime.network-primitives` (6 total, 6 required)
- `runtime.primitive-method-application` (35 total, 35 required)
- `runtime.binding-store` (35 total, 35 required)
- `runtime.permissions` (2 total, 2 required)
- `runtime.regions` (45 total, 45 required)
- `runtime.value-model` (17 total, 17 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W7. Modules, Name Resolution, And Visibility

Rebuild module path validation, name introduction, duplicate detection, scope lookup, qualified resolution, imports, using forms, and accessibility.

Obligation owners:
- `checker.modules` (209 total, 209 required)
- `checker.name-resolution` (307 total, 307 required)
- `checker.visibility` (15 total, 15 required)
- `diagnostics.name-resolution` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W8. Type System, Permissions, And Core Static Semantics

Rebuild type equivalence, subtyping, inference, primitive/composite type well-formedness, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.

Obligation owners:
- `checker.types` (283 total, 181 required)
- `checker.types.primitive` (13 total, 13 required)
- `checker.types.tuples` (41 total, 41 required)
- `checker.types.arrays` (34 total, 34 required)
- `checker.types.slices` (28 total, 28 required)
- `checker.types.ranges` (55 total, 55 required)
- `checker.permissions` (32 total, 32 required)
- `checker.permission-state` (18 total, 18 required)
- `checker.binding-state` (72 total, 72 required)
- `checker.provenance` (51 total, 51 required)
- `checker.regions` (12 total, 12 required)
- `checker.ffi` (1 total, 1 required)
- `diagnostics.types` (2 total, 1 required)
- `diagnostics.permissions` (2 total, 2 required)
- `diagnostics.memory` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W9. Declarations And Type Constructs

Rebuild records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, opaque/refinement/capability classes, procedures, methods, and contracts.

Obligation owners:
- `spec.modal-special` (386 total, 386 required)
- `spec.abstraction-polymorphism` (328 total, 327 required)
- `spec.procedures-contracts` (283 total, 283 required)
- `checker.attributes` (16 total, 16 required)
- `checker.attributes.layout` (10 total, 10 required)
- `checker.attributes.metadata` (23 total, 23 required)
- `checker.attributes.optimization` (2 total, 2 required)
- `diagnostics.attributes` (2 total, 2 required)
- `diagnostics.attributes.layout` (1 total, 1 required)
- `diagnostics.attributes.metadata` (1 total, 1 required)
- `diagnostics.attributes.optimization` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W10. Statements, Expressions, Patterns, Keys, Parallelism, Async, And Comptime

Rebuild all executable language constructs, pattern coverage, key acquisition/release/conflict semantics, structured parallelism, async state machines, and compile-time forms.

Obligation owners:
- `spec.statements` (260 total, 260 required)
- `spec.expressions` (478 total, 475 required)
- `spec.patterns` (161 total, 161 required)
- `spec.key-system` (185 total, 175 required)
- `spec.structured-parallelism` (181 total, 180 required)
- `spec.async` (254 total, 253 required)
- `spec.comptime` (181 total, 181 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W11. FFI, Initialization, Runtime Interface, Cleanup, And Symbols

Rebuild foreign ABI validation, hosted exports, module/global initialization, runtime interface symbols, cleanup/unwind/drop hooks, and symbol naming.

Obligation owners:
- `spec.ffi` (203 total, 203 required)
- `spec.initialization` (102 total, 102 required)
- `spec.runtime-interface` (64 total, 64 required)
- `spec.cleanup` (56 total, 56 required)
- `spec.symbols` (51 total, 51 required)
- `runtime.attributes` (2 total, 2 required)
- `runtime.attributes.layout` (1 total, 1 required)
- `runtime.attributes.metadata` (1 total, 1 required)
- `runtime.attributes.optimization` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### W12. Lowering, Backend, Driver, And Self-Host

Rebuild canonical lowering, IR, LLVM target constants, ABI lowering, object/IR emission, linking, archiving, driver commands, and Stage 0/1/2 fixed-point self-host verification.

Obligation owners:
- `spec.lowering` (158 total, 158 required)
- `spec.backend` (190 total, 190 required)
- `lowering.permissions` (3 total, 3 required)
- `lowering.attributes` (2 total, 2 required)
- `lowering.attributes.layout` (1 total, 1 required)
- `lowering.attributes.metadata` (1 total, 1 required)
- `lowering.attributes.optimization` (1 total, 1 required)
- `backend.llvm-target` (3 total, 3 required)
- `backend.llvm-codegen` (4 total, 4 required)
- `codegen` (51 total, 51 required)
- `project.command-output` (6 total, 6 required)
- `project.output-pipeline` (39 total, 39 required)
- `project.linker` (31 total, 31 required)
- `project.tool-resolution` (7 total, 7 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

## Conformance Test Architecture

Conformance tests are source fixtures and compiler expectations, not external validation scripts. Tests must live under `Tests/` and be grouped by the same construct owners used in Appendix A.

Required fixture classes:

- `Accept`: well-formed programs that must check/build and, where executable, produce expected observable behavior.
- `Reject`: ill-formed programs that must fail with the exact required diagnostic identity.
- `Diagnostic`: rendering, ordering, span, recovery, and max-error-count cases.
- `Phase`: cases that prove phase ordering and that later phases do not execute after earlier rejection.
- `Target`: target-profile, ABI, layout, object naming, link flag, and runtime symbol cases.
- `Bootstrap`: Stage 0/1/2 compiler self-build and fixed-point cases.

Every fixture must record the obligation IDs it covers in a stable metadata header or sidecar expected-output file. If one fixture covers multiple obligations, all covered IDs must be listed. If an obligation cannot be tested with source alone, it must be covered by compiler trace output or Stage 1/2 artifact comparison.

## Completion Criteria

The rebuild is complete only when all of the following are true:

- The repository tree is PascalCase except externally mandated source, ABI, serialized, or command names.
- `Docs/Audit/MigrationLedger.md` contains one accepted entry for every migrated source object and every required obligation owner in Appendix A.
- Stage 0 Cursive compiles the full Ultraviolet runtime, compiler library, and `uv` executable on `x86_64-win64`.
- Stage 1 `uv.exe` recompiles itself and the runtime.
- Stage 2 output is stable against Stage 1 by normalized diagnostics, conformance traces, IR, and artifact fingerprints.
- No reachable duplicate compiler implementation, compatibility shim, fallback path, temporary branch, or test-only path remains.
- Every required obligation ID in Appendix A is implemented, tested, or explicitly classified as outside the compiler/runtime implementation boundary with a spec citation.

## Appendix A - Exact Obligation References By Construct Owner

Reference format: `obligation-id@Linternal_spec_line`. The line number is the `internal_spec_line` from the current obligation ledger, not a Markdown line in this plan. The complete source ledger currently contains 5953 obligations.

### Conformance And Specification Contract

#### `front-matter.document-organization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L19-L19.

- `front-matter.document-organization@L19`

#### `front-matter.feature-section-template`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L77-L77.

- `front-matter.feature-section-template@L77`

#### `front-matter.language-design-contract`

Count: 1 total; 0 required; 1 recommended; 0 informative. Ledger line span: L102-L102.

- `front-matter.language-design-contract@L102`

#### `conformance.definitions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L151-L181.

- `def.Conforming@L151`, `def.WF@L166`, `def.ReqJudgments@L181`

#### `conformance.translation-phases`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L196-L527.

- `def.Phase1Order@L196`, `def.Phase2Order@L211`, `def.Phase3Checks@L226`, `def.Phase3Order@L245`, `def.Phase4Order@L259`, `def.TranslationPhases@L527`

#### `conformance.constructs`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L274-L495.

- `def.TypeAndStatementNodes@L274`, `def.ItemKind@L289`, `def.TopDeclConstructs@L311`, `def.TypeCtor@L325`, `def.TypeConstructs@L366`, `def.PermissionConstructs@L380`, `def.ExprKind@L399`, `def.StmtKind@L440`
- `def.ExprStmtConstructs@L467`, `def.CapConstructs@L481`, `def.Constructs@L495`

#### `conformance.rejection`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L509-L509.

- `Reject-IllFormed@L509`

#### `conformance.behavior-types`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L544-L644.

- `def.StaticJudgmentSet@L544`, `def.StaticRuleSet@L558`, `def.RuleShape@L572`, `def.JudgmentSubjectAndEnvironment@L587`, `def.RuleSubstitutions@L602`, `def.RuleApplies@L616`, `def.PremisesHold@L630`, `def.IllFormed@L644`

#### `conformance.undefinedness`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L658-L722.

- `def.StaticUndefined@L658`, `def.RuleDiagnosticIdentity@L673`, `def.RuleSectionIndex@L689`, `Static-Undefined@L704`, `Static-Undefined-NoCode@L722`

#### `conformance.outside-conformance`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L740-L814.

- `def.OutsideConformance@L740`, `req.ResourceExhaustionOutsideConformance@L814`

#### `conformance.check-kinds`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L755-L798.

- `def.CheckKind@L755`, `def.StaticCheck@L770`, `def.RuntimeCheck@L784`, `def.RuntimeCheckBehavior@L798`

#### `conformance.error-recovery`

Count: 6 total; 5 required; 1 recommended; 0 informative. Ledger line span: L828-L899.

- `def.LexRecovery@L828`, `def.ParseRecovery@L843`, `def.TypeRecovery@L857`, `def.MaxErrorCount@L871`, `def.SuggestedMaxErrorCount@L885`, `def.AbortOnErrorCount@L899`

#### `conformance.document-conventions`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L915-L976.

- `def.NormativeKeywords@L915`, `conformance.RFC2119@L930`, `def.DiagnosticCodeComponents@L945`, `def.DiagnosticCodeFormat@L962`, `def.DiagnosticCodeDigitParts@L976`

#### `conformance.normative-references`

Count: 3 total; 2 required; 0 recommended; 1 informative. Ledger line span: L993-L1036.

- `refs.NormativeRefs@L993`, `refs.ReferenceDetails@L1015`, `refs.Conformance@L1036`

#### `conformance.phase-ordering`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L1053-L1155.

- `def.CompileTimeTranslationPhases@L1053`, `def.ExecuteComptime@L1071`, `req.Phase1BeforePhase2@L1085`, `req.Phase2DeterministicDependencyOrder@L1099`, `req.Phase2DeclarationsBeforePhase3@L1113`, `req.Phase3UsesExpandedModuleSet@L1127`, `req.Phase4OnlyAcceptedProgram@L1141`, `conformance.ComptimeFormsChapterOwner@L1155`

#### `conformance.target-abi`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L1171-L1265.

- `def.TargetProfile@L1171`, `req.TargetProfileResolution@L1186`, `req.TargetProfileNoHostInference@L1204`, `def.TargetArch@L1218`, `def.TargetMachineModel@L1234`, `def.TargetTriple@L1249`, `conformance.LayoutAndABIOwnerSections@L1265`

### Diagnostics

#### `diagnostics.source-spans`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L1283-L1380.

- `def.SourceLocation@L1283`, `def.Span@L1298`, `def.SpanRange@L1313`, `WF-Location@L1327`, `WF-Span@L1345`, `def.ClampSpan@L1363`, `Span-Of@L1380`

#### `diagnostics.token-spans`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L1400-L1481.

- `def.TokenKind@L1400`, `No-Unknown-Ok@L1415`, `def.RawToken@L1433`, `def.Token@L1448`, `Attach-Token-Ok@L1463`, `Attach-Tokens-Ok@L1481`

#### `diagnostics.records`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L1503-L1560.

- `def.DiagnosticSeverity@L1503`, `def.DiagCodeOpt@L1518`, `def.Diagnostic@L1532`, `req.DiagnosticCodeOwnership@L1546`, `def.DiagnosticStream@L1560`

#### `diagnostics.emission`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L1575-L1656.

- `Emit-Append@L1575`, `def.DiagnosticTableColumns@L1592`, `def.EmitImplicit@L1611`, `def.EmitList@L1626`, `def.DiagnosticMessageLookup@L1641`, `def.CompileStatus@L1656`

#### `diagnostics.code-selection`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L1674-L1741.

- `def.SpecCode@L1674`, `Code@L1690`, `conformance.AppendixADiagnosticIndex@L1708`, `DiagId-Code@L1722`, `def.DiagIdCodeMapping@L1741`

#### `diagnostics.ordering`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L1758-L1758.

- `Order@L1758`

#### `diagnostics.rendering`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L1778-L1965.

- `def.DiagnosticRender@L1778`, `def.DiagnosticRenderComponents@L1794`, `def.DiagnosticRenderRich@L1820`, `def.DiagnosticRenderRichHeader@L1841`, `def.DiagnosticSourceRendering@L1857`, `def.TypeRenderLexemes@L1873`, `def.TypeRenderStateSuffixes@L1891`, `def.ParamRender@L1914`
- `def.TypeRender@L1929`, `def.ModalRefRender@L1965`

#### `diagnostics.no-source-span`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L1982-L1982.

- `NoSpan-External@L1982`

#### `diagnostics.project`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L6767-L6767.

- `diagnostics.ProjectDiagnostics@L6767`

#### `diagnostics.source-loading`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L7832-L7961.

- `def.SourceLoadTemporarySource@L7832`, `def.SourceLoadDiagnosticOffsets@L7846`, `def.SpanAtIndex@L7861`, `def.SpanAtLineStart@L7875`, `req.LeadingBOMWarningPersistence@L7893`, `Span-BOM-Warn@L7907`, `Span-BOM-Embedded@L7925`, `Span-Prohibited@L7943`
- `NoSpan-Decode@L7961`

#### `diagnostics.source-lexical`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L10319-L10319.

- `diagnostics.SourceLexicalDiagnostics@L10319`

#### `diagnostics.parsing`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L11997-L12045.

- `def.ParsingDiagnosticRules@L11997`, `def.GenericParseRules@L12011`, `Parse-Syntax-Err@L12025`, `diagnostics.ParsingDiagnostics@L12045`

#### `diagnostics.memory`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L18366-L18366.

- `diagnostics.RuntimeStateAndMemoryDiagnostics@L18366`

#### `diagnostics.name-resolution`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L23703-L23703.

- `diagnostics.NameResolutionAndReservedNames@L23703`

#### `diagnostics.types`

Count: 2 total; 1 required; 0 recommended; 0 informative. Ledger line span: L26206-L40949.

- `diagnostics.CoreTypeDiagnostics@L26206`, `diagnostics.DataTypesSupplement@L40949`

#### `diagnostics.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27115-L27324.

- `diagnostics.AttributeDiagnostics@L27115`, `diagnostics.VendorAttributeDiagnostics@L27324`

#### `diagnostics.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27611-L27611.

- `diagnostics.LayoutAttributeDiagnostics@L27611`

#### `diagnostics.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27756-L27756.

- `diagnostics.OptimizationAttributeDiagnostics@L27756`

#### `diagnostics.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28237-L28237.

- `diagnostics.DiagnosticsMetadataAttributes@L28237`

#### `diagnostics.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28549-L28702.

- `req.PermissionFormsDiagnosticOwnership@L28549`, `req.AliasExclusivityDiagnosticOwnership@L28702`

### Project, Build, Output, And Linking

#### `project.core-records`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L2004-L2037.

- `def.AssemblyAndLinkKinds@L2004`, `def.ProjectRecord@L2019`, `def.AssemblyClassification@L2037`

#### `project.validation-scope`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L2056-L2056.

- `def.ProjectValidationScope@L2056`

#### `project.command-output`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L2073-L2150.

- `def.DumpProjectOutput@L2073`, `def.ProjectSummaryOutput@L2090`, `def.OutputSummary@L2104`, `def.LinkOutputSummary@L2118`, `def.IROpt@L2134`, `def.ImportLibOpt@L2150`

#### `project.manifest-parser`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L2168-L2219.

- `def.ManifestParsing@L2168`, `Parse-Manifest-Ok@L2183`, `Parse-Manifest-Missing@L2201`, `Parse-Manifest-Err@L2219`

#### `project.manifest-loader`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L2237-L2267.

- `conformance.ManifestRequired@L2237`, `req.ManifestHostPathResolution@L2252`, `req.ManifestNoAdditionalCaseVerification@L2267`

#### `project.root-discovery`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L2281-L3491.

- `req.ProjectRootResolveInputPath@L2281`, `req.ProjectRootFileInputStartsAtParent@L2296`, `req.ProjectRootDirectoryInputStartsAtResolvedPath@L2310`, `def.FindProjectRoot@L2324`, `WF-Project-Root@L3491`

#### `project.manifest-schema`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L2338-L2538.

- `def.ManifestSchema@L2338`, `WF-Assembly-Name@L2358`, `WF-Assembly-Name-Err@L2376`, `WF-Assembly-Kind@L2394`, `WF-Assembly-Kind-Err@L2412`, `WF-Assembly-Root-Path@L2430`, `WF-Assembly-Root-Path-Err@L2448`, `WF-Assembly-OutDir-Path@L2466`
- `WF-Assembly-OutDir-Path-Err@L2484`, `WF-Assembly-EmitIR@L2502`, `WF-Assembly-EmitIR-Err@L2520`, `def.AsmLinkKind@L2538`

#### `project.manifest-validation`

Count: 31 total; 31 required; 0 recommended; 0 informative. Ledger line span: L2555-L3723.

- `def.ManifestTableProjections@L2555`, `def.ManifestTopKeys@L2575`, `WF-TopKeys@L2589`, `WF-TopKeys-Err@L2607`, `WF-Assembly-Table@L2625`, `WF-Assembly-Table-Err@L2643`, `WF-Assembly-Count@L2661`, `WF-Assembly-Count-Err@L2679`
- `WF-Assembly-Name-Dup@L2697`, `WF-Assembly-Name-Dup-Err@L2715`, `def.ManifestAssemblyKeys@L2733`, `WF-Assembly-Keys@L2748`, `WF-Assembly-Keys-Err@L2766`, `WF-Assembly-Required-Types@L2784`, `WF-Assembly-Required-Types-Err@L2802`, `WF-Assembly-Optional-Types@L2820`
- `WF-Assembly-OutDirType-Err@L2838`, `WF-Assembly-EmitIRType@L2856`, `WF-Assembly-EmitIRType-Err@L2873`, `WF-Assembly-LinkKindType@L2891`, `WF-Assembly-LinkKindType-Err@L2908`, `WF-Assembly-LinkKind@L2926`, `WF-Assembly-LinkKind-Err@L2944`, `WF-Assembly-LinkKind-Use-Err@L2962`
- `def.ChecksAsm@L3630`, `def.BaseChecks@L3645`, `def.AsmChecks@L3659`, `def.ManifestChecks@L3675`, `def.FirstFail@L3689`, `ValidateManifest-Ok@L3705`, `ValidateManifest-Err@L3723`

#### `project.toolchain-config`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L2980-L3061.

- `def.ToolchainKeys@L2980`, `def.ToolchainTargetProfileOk@L2995`, `WF-Toolchain@L3009`, `WF-Toolchain-Err@L3027`, `def.ToolchainConfig@L3045`, `def.SelectedTargetProfile@L3061`

#### `project.build-config`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L3078-L3129.

- `def.BuildConfiguration@L3078`, `WF-Build@L3093`, `WF-Build-Err@L3111`, `def.BuildConfigDefaults@L3129`

#### `project.path-resolution`

Count: 20 total; 20 required; 0 recommended; 0 informative. Ledger line span: L3146-L3459.

- `def.PathRootPredicates@L3146`, `def.PathRootTagAndTail@L3165`, `def.PathSegments@L3188`, `def.PathComponents@L3202`, `def.JoinComp@L3218`, `def.Join@L3237`, `def.AbsPath@L3253`, `def.PathFunctionTypes@L3268`
- `def.PathPrefix@L3284`, `def.Normalize@L3298`, `def.Under@L3312`, `def.Canon@L3326`, `def.Drop@L3341`, `def.RelativePathComputation@L3355`, `def.Basename@L3369`, `def.FileExt@L3386`
- `Resolve-Canonical@L3405`, `Resolve-Canonical-Err@L3423`, `WF-RelPath@L3441`, `WF-RelPath-Err@L3459`

#### `project.context`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L3477-L3477.

- `def.ProjectContext@L3477`

#### `project.assembly-loader`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L3511-L3979.

- `WF-Assembly@L3511`, `BuildAssembly-Ok@L3925`, `BuildAssembly-Err-Resolve@L3943`, `BuildAssembly-Err-Root@L3961`, `BuildAssembly-Err-Modules@L3979`

#### `project.loader-state-machine`

Count: 14 total; 14 required; 0 recommended; 0 informative. Ledger line span: L3529-L4017.

- `def.AssemblyTarget@L3529`, `def.ProjLoadState@L3544`, `Step-Parse@L3558`, `Step-Parse-Err@L3576`, `Step-Validate@L3594`, `Step-Validate-Err@L3612`, `Step-Asm-Init@L3741`, `Step-Asm-Cons@L3759`
- `Step-Asm-Err@L3777`, `Step-Asm-Done@L3795`, `Step-Asm-Own-Err@L3813`, `Step-Asm-Done-Err@L3831`, `LoadProject-Ok@L3999`, `LoadProject-Err@L4017`

#### `project.assembly-selection`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L3851-L3905.

- `Select-Only@L3851`, `Select-Only-Exe@L3869`, `Select-By-Name@L3887`, `Select-Err@L3905`

#### `project.assembly-graph`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L4035-L4667.

- `def.AsmDeps@L4035`, `req.AssemblyGraphRejectExecutableImports@L4051`, `req.AssemblyGraphRejectLibraryCycles@L4065`, `req.DependencyAssemblyNoFinalArtifact@L4079`, `def.AssemblyProject@L4501`, `def.ModulePaths@L4515`, `def.AsmImportGraph@L4529`, `def.GraphAccessors@L4545`
- `def.GraphPathReach@L4560`, `def.NoLibraryInterior@L4575`, `def.LibraryBoundaryCycle@L4589`, `def.ImportsExecutable@L4603`, `def.HostedLibraryImportsLinkedLibrary@L4617`, `Assembly-Graph-Ok@L4631`, `Assembly-Graph-Err@L4649`, `Assembly-Graph-HostedImport-Err@L4667`

#### `project.deterministic-ordering`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L4095-L4234.

- `def.FoldPath@L4095`, `def.FileKey@L4109`, `def.FileOrdering@L4125`, `FileOrder-Rel-Fail@L4139`, `def.Fold@L4157`, `def.DirKey@L4172`, `def.DirectoryOrdering@L4188`, `def.DirSeq@L4202`
- `DirSeq-Read-Err@L4216`, `DirSeq-Rel-Fail@L4234`

#### `project.source-roots`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L4254-L4288.

- `def.SourceRootDirectories@L4254`, `WF-Source-Root@L4270`, `WF-Source-Root-Err@L4288`

#### `project.module-discovery`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L4306-L4402.

- `Module-Dir@L4306`, `def.ModuleDirectoryFiles@L4324`, `def.CompilationUnits@L4338`, `CompilationUnit-Rel-Fail@L4352`, `refs.ModuleDiscoverySection@L4370`, `Modules-Ok@L4384`, `Modules-Err@L4402`

#### `project.assembly-ownership`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L4420-L4481.

- `def.AssemblySourceRoots@L4420`, `def.OwnerRoot@L4435`, `def.OwnedModules@L4449`, `OwnAssemblies-Ok@L4463`, `WF-Assembly-Root-Owner-Ambiguous@L4481`

#### `project.output-artifacts`

Count: 43 total; 43 required; 0 recommended; 0 informative. Ledger line span: L4685-L5329.

- `def.EmitAssemblies@L4685`, `def.ImportedLibraries@L4699`, `def.LibraryPred@L4714`, `def.LibraryReady@L4728`, `def.LibraryTopo@L4742`, `def.ReadyLexLeast@L4756`, `EmitModuleList-Ok@L4770`, `def.RequiredOutputs@L4788`
- `def.IRSet@L4802`, `def.PrimaryArtifactSet@L4818`, `def.ImportLibSet@L4834`, `def.OutputRoot@L4850`, `def.OutputHygiene@L4867`, `def.OutputPathsRoot@L4882`, `def.OutputPathsDirectories@L4898`, `def.ProjectOutputBinding@L4915`
- `def.PathToPrefix@L4929`, `def.BMap@L4944`, `def.ModulePathMangle@L4960`, `def.ObjectPath@L4975`, `def.FinalArtifactLibraryName@L4989`, `def.FinalArtifactNames@L5004`, `def.ModuleEmissionPathContext@L5021`, `def.ModuleEmissionOrder@L5037`
- `def.Utf8LexLess@L5052`, `ModuleList-Ok@L5066`, `def.ArtifactPathContext@L5084`, `def.EmitIRExtension@L5100`, `def.ObjPath@L5116`, `def.IRPath@L5130`, `def.ExePath@L5144`, `def.SharedLibPath@L5160`
- `def.StaticLibPath@L5176`, `def.ImportLibPath@L5192`, `def.PrimaryArtifact@L5208`, `def.ArtifactOutputDirectoryUse@L5226`, `def.ObjPaths@L5241`, `def.IRPaths@L5255`, `def.LibraryArtifactInputs@L5269`, `def.EmitModuleIndex@L5283`
- `def.Pad4@L5299`, `def.SymbolName@L5313`, `def.Trunc8@L5329`

#### `project.output-pipeline`

Count: 39 total; 39 required; 0 recommended; 0 informative. Ledger line span: L5425-L6590.

- `def.WriteFileOk@L5425`, `def.Overwrites@L5440`, `def.EnsureDir@L5454`, `def.PathKindChecks@L5469`, `Emit-Objects-Empty@L5486`, `Emit-Objects-Cons@L5503`, `Emit-IR-None@L5523`, `Emit-IR-Cons-LL@L5541`
- `Emit-IR-Cons-BC@L5559`, `def.EmitIRFailLL@L5577`, `def.EmitIRFailBC@L5591`, `Emit-IR-Err@L5609`, `Finalize-Link@L6129`, `Finalize-Archive@L6147`, `def.OutputPipelineBigStepInputs@L6165`, `Output-Pipeline-Linkable@L6182`
- `Output-Pipeline-Dependency@L6200`, `Output-Pipeline-Err@L6218`, `def.OutputPipelineState@L6236`, `def.OutputPipelineSmallStepInputs@L6251`, `Out-Start@L6267`, `Out-Dirs-Ok@L6284`, `Out-Dirs-Err@L6302`, `Out-Obj-Collision@L6320`
- `Out-Obj-Cons@L6338`, `Out-Obj-Err@L6356`, `Out-Obj-Done@L6374`, `Out-IR-None-Finalize@L6391`, `Out-IR-None-NoFinalize@L6409`, `Out-IR-Collision@L6427`, `Out-IR-Cons-LL@L6445`, `Out-IR-Cons-BC@L6463`
- `Out-IR-Err@L6481`, `Out-IR-Done-Finalize@L6500`, `Out-IR-Done-NoFinalize@L6518`, `Out-Final-Link-Ok@L6536`, `Out-Final-Link-Err@L6554`, `Out-Final-Archive-Ok@L6572`, `Out-Final-Archive-Err@L6590`

#### `project.linker`

Count: 31 total; 31 required; 0 recommended; 0 informative. Ledger line span: L5627-L6111.

- `def.LinkJudg@L5627`, `def.RuntimeLibName@L5641`, `def.CompilerExecutableDir@L5655`, `def.CompilerSidecarLayoutPredicates@L5669`, `def.CompilerSupportRoot@L5684`, `def.CompilerRuntimeLibPath@L5701`, `def.RuntimeLibPath@L5716`, `ResolveRuntimeLib-Ok@L5732`
- `ResolveRuntimeLib-Err@L5750`, `Build-LibrariesSeq-Empty@L5768`, `Build-LibrariesSeq-Cons@L5785`, `Build-Libraries-Ok@L5803`, `def.LinkerSymbols@L5821`, `def.LinkObjs@L5837`, `def.LinkMode@L5851`, `def.LinkOutputPath@L5868`
- `def.LinkImportLibOpt@L5885`, `def.LinkInputs@L5901`, `def.ArchiveInputs@L5915`, `def.LinkFlags@L5929`, `def.ArchiveFlags@L5943`, `def.LinkArgsOk@L5957`, `def.ArchiveArgsOk@L5971`, `Link-Ok@L5985`
- `Link-NotFound@L6003`, `Link-Runtime-Missing@L6021`, `Link-Runtime-Incompatible@L6039`, `Link-Fail@L6057`, `Archive-Ok@L6075`, `Archive-NotFound@L6093`, `Archive-Fail@L6111`

#### `project.tool-resolution`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L6610-L6711.

- `def.SearchDirs@L6610`, `def.CompilerToolBinDir@L6627`, `def.ToolVersion@L6643`, `ResolveTool-Ok@L6657`, `ResolveTool-Err-Linker@L6675`, `ResolveTool-Err-Archiver@L6693`, `ResolveTool-Err-IR@L6711`

### Source Text And Lexer

#### `lexer.source-loading`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L6810-L7812.

- `def.SourceFileRecord@L6810`, `def.SourceFileDerivedFields@L6825`, `Decode-Ok@L7003`, `Decode-Err@L7021`, `def.StripLeadBOM@L7039`, `StripBOM-Empty@L7055`, `StripBOM-None@L7072`, `StripBOM-Start@L7090`
- `StripBOM-Embedded@L7108`, `def.LineEndingConstants@L7128`, `Norm-Empty@L7143`, `Norm-CRLF@L7160`, `Norm-CR@L7178`, `Norm-LF@L7196`, `Norm-Other@L7214`, `def.Utf8Offsets@L7232`
- `def.LineStarts@L7248`, `def.LineCount@L7262`, `def.LocateLineColumn@L7276`, `def.ProhibitedCodePoints@L7297`, `def.LiteralSpan@L7311`, `WF-Prohibited@L7325`, `def.SourceLoadState@L7599`, `def.SourceLoadStateComponentDomains@L7613`
- `Step-Size@L7631`, `Step-Decode@L7648`, `Step-Decode-Err@L7666`, `Step-BOM@L7684`, `Step-Norm@L7702`, `Step-EmbeddedBOM-Err@L7720`, `Step-LineMap@L7738`, `Step-Prohibited@L7756`
- `Step-Prohibited-Err@L7774`, `LoadSource-Ok@L7794`, `LoadSource-Err@L7812`

#### `lexer.unicode`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L6841-L7403.

- `def.UnicodeScalarDomains@L6841`, `def.Utf8Len@L6860`, `def.EncodeUTF8Scalar@L6878`, `def.EncodeUTF8Sequence@L6896`, `def.DecodeUTF8@L6911`, `def.Utf8Valid@L6925`, `def.Utf8@L6939`, `def.NormalizeOutsideIdentifiers@L6955`
- `def.NFC@L7345`, `def.CaseFold@L7359`, `def.IdentifierKeys@L7373`, `def.ModulePathKeys@L7388`, `req.NFCAndCaseFoldTotality@L7403`

#### `lexer.unicode-security`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L6972-L6987.

- `def.LexSensitivePos@L6972`, `req.LexSecureSensitivePositions@L6987`

#### `lexer.statement-termination`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L7420-L7579.

- `def.TokenizeOutputConstraints@L7420`, `def.NestingDepth@L7435`, `def.PrevNextNonNewlineTokens@L7454`, `def.NewlineContinuationTokenSets@L7471`, `def.Continue@L7489`, `def.FilterContinuedNewlines@L7503`, `def.StatementTerminatorPredicates@L7517`, `req.RangeContinuationAcrossNewline@L7537`
- `req.BraceNewlineContinuation@L7551`, `req.CommaStatementTermination@L7565`, `Missing-Terminator-Err@L7579`

#### `lexer.records`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L7982-L8117.

- `def.LexerInput@L7982`, `def.LexerOutput@L8000`, `def.TokenEOF@L8016`, `def.LexemeBinding@L8032`, `def.DocCommentRecord@L8048`, `def.DocCommentBody@L8065`, `def.NewlineTokens@L8086`, `def.CommentTokenExclusion@L8102`
- `def.LexerIndicesAndLexemes@L8117`

#### `lexer.character-classes`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L8142-L8254.

- `def.CharacterClassInput@L8142`, `def.Whitespace@L8156`, `def.LineFeed@L8171`, `def.IdentifierCharacters@L8186`, `def.XIDVersion@L8205`, `def.NonCharacter@L8221`, `def.DigitCharacters@L8236`, `def.SensitiveCharacters@L8254`

#### `lexer.reserved-lexemes`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L8271-L8348.

- `def.ReservedKeywords@L8271`, `def.FutureReserved@L8286`, `def.KeywordPredicate@L8300`, `def.ReservedNamespaces@L8315`, `def.UniverseProtectedBindings@L8332`, `req.ReservedPredicateNames@L8348`

#### `lexer.tokens`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L8364-L10851.

- `def.TokenKinds@L8364`, `def.OperatorSet@L8378`, `def.PunctuatorSet@L8393`, `def.OperatorPunctuatorDisjoint@L8408`, `req.FixedIdentifiersTokenizedAsIdentifiers@L10851`

#### `lexer.comments`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L8424-L8594.

- `def.CommentScanInput@L8424`, `Scan-Line-Comment@L8440`, `def.DocCommentClassification@L8458`, `Doc-Comment@L8474`, `def.LineCommentSkipping@L8492`, `def.BlockCommentState@L8507`, `Block-Start@L8522`, `Block-End@L8540`
- `Block-Done@L8558`, `Block-Step@L8576`, `Block-Comment-Unterminated@L8594`

#### `lexer.literals`

Count: 53 total; 53 required; 0 recommended; 0 informative. Ledger line span: L8614-L9476.

- `def.LiteralLexingInput@L8614`, `grammar.LiteralLexing@L8630`, `conformance.FloatSuffixDefaulting@L8667`, `def.SimpleEscape@L8682`, `def.ByteEscapeValidity@L8698`, `def.UnicodeEscapeValidity@L8712`, `def.StringCharacterValidity@L8726`, `def.CharacterLiteralContentValidity@L8740`
- `def.QuotedLiteralCharacterUnits@L8754`, `def.NumericSuffixSets@L8769`, `def.StringHelpers@L8787`, `def.HexDigitValue@L8808`, `def.DecimalDigitValue@L8825`, `def.OctalDigitValue@L8839`, `def.BinaryDigitValue@L8853`, `def.NumericValues@L8867`
- `def.NumericUnderscorePredicates@L8883`, `def.NumericDigitRuns@L8902`, `def.SuffixMatch@L8920`, `def.ExponentScan@L8934`, `def.DecCoreEnd@L8954`, `req.DecimalRangeNoFloatCore@L8970`, `def.NumericCoreEnd@L8984`, `def.NumericScanEnd@L9002`
- `def.NumericFloatCorePredicates@L9016`, `def.NumericKind@L9032`, `Lex-Int@L9048`, `Lex-Float@L9066`, `def.NumericLexemeValidation@L9084`, `Lex-Numeric-Err@L9099`, `def.DecimalLeadingZeros@L9117`, `Warn-DecimalLeadingZero@L9133`
- `def.EscapeSequences@L9150`, `Lex-String@L9173`, `def.BackslashCount@L9191`, `def.StringTerminator@L9205`, `def.LineFeedOrEOFBeforeClose@L9220`, `def.EscapeMatch@L9234`, `def.BadEscapeAt@L9248`, `def.FirstBadStringEscape@L9262`
- `Lex-String-Unterminated@L9276`, `Lex-String-BadEscape@L9294`, `Lex-Char@L9312`, `def.CharacterLiteralValueRange@L9330`, `def.CharacterLiteralRepresentation@L9345`, `def.CharacterLiteralTerminator@L9361`, `def.FirstBadCharEscape@L9376`, `def.CharacterLiteralInvalid@L9390`
- `def.CharacterScalarCount@L9404`, `Lex-Char-Unterminated@L9422`, `Lex-Char-BadEscape@L9440`, `Lex-Char-Invalid@L9458`, `def.LiteralTokenizationHelpers@L9476`

#### `lexer.identifiers`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L9497-L9580.

- `def.IdentifierLexingInput@L9497`, `def.IdentifierScan@L9511`, `Lex-Identifier@L9526`, `Lex-Ident-InvalidUnicode@L9544`, `Lex-Ident-Token@L9562`, `def.KeywordClassification@L9580`

#### `lexer.operators`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L9601-L9631.

- `def.OperatorPunctuatorSets@L9601`, `def.OperatorPunctuatorMatches@L9616`, `def.OperatorPunctuatorTokenCandidates@L9631`

#### `lexer.maximal-munch`

Count: 9 total; 9 required; 0 recommended; 0 informative. Ledger line span: L9648-L9782.

- `def.MaximalMunchInput@L9648`, `def.IsQuote@L9662`, `def.TokenCandidates@L9676`, `def.LongestCandidates@L9695`, `def.KindPriority@L9709`, `def.PickLongest@L9732`, `Max-Munch@L9746`, `Max-Munch-Err@L9764`
- `def.GenericCloseException@L9782`

#### `lexer.security`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L9798-L9982.

- `def.LexicalSecurityInputs@L9798`, `def.LiteralAndCommentRanges@L9813`, `def.SensitivePositionsInSpan@L9832`, `def.UnsafeTokenSpans@L9847`, `def.LexicalSecurityCheck@L9879`, `LexSecure-Err@L9894`, `LexSecure-Warn@L9912`, `def.ConfusableIdentifierChecks@L9930`
- `Confusable-Err@L9946`, `MixedScript-Err@L9964`, `def.LexicalSecurityDiagnosticSpans@L9982`

#### `lexer.tokenization`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L9999-L10299.

- `def.LexState@L9999`, `Lex-Start@L10015`, `Lex-End@L10032`, `Lex-Whitespace@L10050`, `Lex-Newline@L10068`, `Lex-Line-Comment@L10086`, `Lex-Doc-Comment@L10104`, `Lex-Block-Comment@L10122`
- `Lex-String-Unterminated-Recover@L10140`, `Lex-Char-Unterminated-Recover@L10158`, `Lex-Sensitive@L10176`, `def.SensitiveTokenPositions@L10194`, `conformance.TupleProjectionLexicalDisambiguation@L10210`, `Lex-Token@L10225`, `Lex-Token-Err@L10243`, `Tokenize-Ok@L10263`
- `Tokenize-Secure-Err@L10281`, `Tokenize-Err@L10299`

### Parser, AST, Attributes, And Grammar

#### `parser.phase`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L10357-L10422.

- `def.ParseInputs@L10357`, `def.ParseOutputs@L10371`, `Phase1-File@L10387`, `Phase1-Forward-Refs@L10405`, `refs.FeatureParseRules@L10422`

#### `parser.ast`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L10438-L10591.

- `def.ASTNodeUniverse@L10438`, `def.ASTParseCtorDefaults@L10454`, `def.DocList@L10480`, `def.ASTItemSet@L10494`, `def.ErrorItem@L10508`, `def.TypeASTVariants@L10525`, `def.TypeASTRecordShapes@L10540`, `def.SharedDeps@L10557`
- `def.TypePermissionAndStateOptions@L10572`, `def.ParamTypeMode@L10591`

#### `parser.state`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L10609-L10777.

- `def.ParserStateRecord@L10609`, `def.ParserStateAccessors@L10624`, `def.ParserTok@L10643`, `def.ParserSourceAndEOFSpan@L10660`, `def.ParserStateOperations@L10675`, `def.ParserIndexInvariant@L10691`, `def.ParserAdvanceOrEOF@L10706`, `def.ParserSpanBetween@L10722`
- `def.SplitSpan2@L10740`, `def.SplitShiftR@L10761`, `def.ParseJudgmentSet@L10777`

#### `parser.shared-grammar`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L10794-L10898.

- `def.ParserLexemePredicates@L10794`, `def.ContextualKeywords@L10813`, `def.FixedIdentifierLexemes@L10830`, `def.UnionPropagationTokens@L10865`, `def.TypeTokenPredicates@L10881`, `refs.TrailingCommaRules@L10898`

#### `parser.shared-helpers`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L10912-L11200.

- `Parse-Ident@L10912`, `Parse-Ident-Err@L10930`, `Parse-TypePath@L10948`, `Parse-ClassPath@L10966`, `Parse-TypePathTail-End@L10984`, `Parse-TypePathTail-Cons@L11002`, `Parse-QualifiedHead@L11020`, `Parse-Vis-Opt@L11038`
- `Parse-Vis-Default@L11056`, `Parse-ModalOpt-Yes@L11074`, `Parse-ModalOpt-No@L11092`, `Parse-AliasOpt-None@L11110`, `Parse-AliasOpt-Yes@L11128`, `Parse-TypeAnnotOpt-None@L11146`, `Parse-TypeAnnotOpt-Yes@L11164`, `Parse-KeyBoundaryOpt-Yes@L11182`
- `Parse-KeyBoundaryOpt-No@L11200`

#### `parser.token-consumption`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L11220-L11302.

- `def.TokenConsumptionState@L11220`, `def.ParseRejectRules@L11234`, `Tok-Consume-Kind@L11248`, `Tok-Consume-Keyword@L11266`, `Tok-Consume-Operator@L11284`, `Tok-Consume-Punct@L11302`

#### `parser.list-parsing`

Count: 11 total; 11 required; 0 recommended; 0 informative. Ledger line span: L11320-L11472.

- `def.ListParsingState@L11320`, `List-Start@L11335`, `List-Cons@L11352`, `List-Done@L11370`, `def.ListEndSet@L11388`, `req.ListOpeningParserStateConvention@L11402`, `def.TrailingComma@L11416`, `def.TokensBetween@L11430`
- `def.TokLine@L11444`, `def.TrailingCommaAllowed@L11458`, `Trailing-Comma-Err@L11472`

#### `parser.file`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L11492-L11509.

- `def.ParseFileBigStep@L11492`, `ParseFile-Ok@L11509`

#### `parser.items`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L11529-L11547.

- `ParseItems-Empty@L11529`, `ParseItems-Cons@L11547`

#### `parser.terminators`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L11567-L11686.

- `def.StatementTerminatorTokens@L11567`, `def.StatementRequiresTerminator@L11582`, `ConsumeTerminatorOpt-Req-Yes@L11596`, `ConsumeTerminatorOpt-Req-No@L11614`, `ConsumeTerminatorOpt-Opt-Yes@L11632`, `ConsumeTerminatorOpt-Opt-No@L11650`, `ConsumeTerminatorReq-Yes@L11668`, `ConsumeTerminatorReq-No@L11686`

#### `parser.doc-comments`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L11706-L11772.

- `def.DocCommentAssociation@L11706`, `Attach-Doc-Line@L11721`, `def.LineDocTargets@L11739`, `Attach-Doc-Module@L11754`, `def.ModuleDocs@L11772`

#### `parser.recovery`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L11790-L11980.

- `def.StatementSynchronizationSet@L11790`, `def.ItemSynchronizationSet@L11806`, `def.TypeSynchronizationSet@L11822`, `Sync-Stmt-Stop@L11836`, `Sync-Stmt-Consume@L11854`, `Sync-Stmt-Advance@L11872`, `Sync-Item-Stop@L11890`, `Sync-Item-Advance@L11908`
- `Sync-Type-Stop@L11926`, `Sync-Type-Consume@L11944`, `Sync-Type-Advance@L11962`, `def.ParseErrorRuleBindings@L11980`

#### `parser.attributes`

Count: 42 total; 42 required; 0 recommended; 0 informative. Ledger line span: L26234-L27256.

- `grammar.AttributeSyntaxAndPlacement@L26234`, `req.AttributeReservedLeafNames@L26265`, `req.AttributeImmediateTargetPlacement@L26279`, `Parse-AttrListOpt-None@L26295`, `Parse-AttrListOpt-Yes@L26313`, `Parse-AttrList-Cons@L26331`, `Parse-AttrListTail-End@L26349`, `Parse-AttrListTail-Cons@L26367`
- `Parse-AttrBlock@L26385`, `Parse-AttrSpecList-Cons@L26403`, `Parse-AttrSpecListTail-End@L26421`, `Parse-AttrSpecListTail-TrailingComma@L26439`, `Parse-AttrSpecListTail-Comma@L26457`, `Parse-AttrSpec@L26475`, `Parse-AttrArgsOpt-None@L26493`, `Parse-AttrArgsOpt-Yes@L26511`
- `Parse-AttrArgList-Cons@L26529`, `Parse-AttrArgListTail-End@L26547`, `Parse-AttrArgListTail-TrailingComma@L26565`, `Parse-AttrArgListTail-Comma@L26583`, `Parse-AttrArg-Named-Literal@L26601`, `Parse-AttrArg-Named-Ident@L26619`, `Parse-AttrArg-Named-Call@L26637`, `Parse-AttrArg-Literal@L26655`
- `Parse-AttrArg-Ident@L26673`, `def.AttributeAstRepresentation@L26694`, `def.AttributeVendorPrefixAst@L26713`, `def.AttributeArgumentAst@L26727`, `def.AttributeSpecAst@L26741`, `def.AttributeListAst@L26755`, `def.ExpressionAttributes@L26770`, `def.AttachExpressionAttributes@L26784`
- `def.ItemAttributeList@L26798`, `def.AttributeByName@L26813`, `conformance.VendorAttributeSyntaxReuse@L27135`, `req.VendorAttributeParserReuse@L27153`, `def.AttributeLeafToken@L27167`, `Parse-AttrName-Plain@L27181`, `Parse-AttrName-Vendor@L27199`, `Parse-VendorPrefixTail-End@L27217`
- `Parse-VendorPrefixTail-Cons@L27235`, `def.VendorAttributeAst@L27256`

#### `parser.attributes.layout`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27344-L27383.

- `grammar.LayoutAttributeSyntax@L27344`, `req.LayoutAttributeParserReuse@L27367`, `def.LayoutAttributeAstAttachment@L27383`

#### `parser.attributes.optimization`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27633-L27672.

- `grammar.OptimizationAttributeSyntax@L27633`, `req.OptimizationAttributeParserReuse@L27656`, `def.OptimizationAttributeAstAttachment@L27672`

#### `parser.attributes.metadata`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L27774-L27823.

- `req.DiagnosticsMetadataSyntaxParsingAst@L27774`, `req.DiagnosticsMetadataParserReuse@L27792`, `def.ExpressionAttributeList@L27808`, `def.ExpressionAttributeByName@L27823`

#### `parser.permissions`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L28262-L28752.

- `grammar.PermissionFormsSyntax@L28262`, `req.PermissionQualifierTypeGrammarPlacement@L28281`, `req.PermissionParserOwnership@L28297`, `req.ParseReceiverCanonicalOwner@L28311`, `def.PermissionAstForms@L28327`, `def.PermissionQualifiedTypeAst@L28345`, `req.AliasExclusivityNoParsingRules@L28584`, `req.AliasExclusivityNoAstForms@L28600`
- `req.BindingActivityNoParsingRules@L28736`, `req.BindingActivityNoAstNode@L28752`

#### `parser`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L28918-L30777.

- `req.PermissionAdmissibilityNoAdditionalParsing@L28918`, `grammar.ImportDeclarationSyntax@L29155`, `req.ImportDeclarationParserBranch@L29175`, `grammar.UsingDeclarationSyntax@L29399`, `grammar.StaticDeclarationSyntax@L29909`, `req.StaticDeclParserOwnership@L29942`, `grammar.ExternBlockShellSyntax@L30777`

#### `parser.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L30797-L30797.

- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L30797`

#### `parser.modules`

Count: 58 total; 58 required; 0 recommended; 0 informative. Ledger line span: L31081-L32075.

- `grammar.ModulePathSyntax@L31081`, `req.ModuleToFileMappingNoSurfaceSyntax@L31100`, `req.ModulePathParserOwnership@L31114`, `Parse-ModulePath@L31130`, `Parse-ModulePathTail-End@L31148`, `Parse-ModulePathTail-Cons@L31166`, `def.PathAstAliases@L31184`, `def.ASTModule@L31205`
- `def.ASTFile@L31222`, `Module-Path-Root@L31258`, `Module-Path-Rel@L31276`, `Module-Path-Rel-Fail@L31294`, `def.ModuleDirOf@L31312`, `def.ProjectModuleView@L31327`, `def.SourceRootOfModule@L31345`, `def.WFModulePathJudgementSet@L31359`
- `WF-Module-Path-Ok@L31373`, `WF-Module-Path-Reserved@L31391`, `WF-Module-Path-Ident-Err@L31409`, `WF-Module-Path-Collision@L31427`, `def.ModuleAggregationInputsOutputs@L31445`, `def.ModuleMap@L31462`, `def.ASTModuleOfProjectPath@L31476`, `def.PathOfModule@L31490`
- `def.ParseModuleRuleReference@L31520`, `def.ParseModuleBigStepInputs@L31534`, `ReadBytes-Ok@L31551`, `ReadBytes-Err@L31569`, `def.BytesOfFile@L31587`, `ParseModule-Ok@L31601`, `ParseModule-Err-Read@L31619`, `ParseModule-Err-Load@L31637`
- `req.LoadSourceShortCircuit@L31655`, `ParseModule-Err-Unit@L31670`, `ParseModule-Err-Parse@L31688`, `def.ParseFileBestEffort@L31706`, `def.ParseFileOk@L31720`, `def.ParseFileDiag@L31734`, `def.HasErrorDiagnostics@L31748`, `def.ModState@L31762`
- `Mod-Start@L31777`, `Mod-Start-Err-Unit@L31795`, `Mod-Scan@L31813`, `Mod-Scan-Err-Read@L31831`, `Mod-Scan-Err-Load@L31849`, `Mod-Scan-Err-Parse@L31867`, `Mod-Done@L31885`, `def.ParseModulesBigStepInputs@L31902`
- `ParseModules-Ok@L31918`, `ParseModules-Err@L31936`, `def.DiscState@L31954`, `Disc-Start@L31968`, `Disc-Skip@L31985`, `Disc-Add@L32003`, `Disc-Collision@L32021`, `Disc-Invalid-Component@L32039`
- `Disc-Rel-Fail@L32057`, `Disc-Done@L32075`

#### `parser.types`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L34302-L40557.

- `grammar.PrimitiveTypeSyntax@L34302`, `def.PrimLexemeSet@L34329`, `grammar.TupleSyntax@L34643`, `req.TupleSingletonCommaIllFormed@L34667`, `def.TupleScanDepthAndStep@L34779`, `def.TupleScanPredicates@L34795`, `grammar.ArraySyntax@L35506`, `grammar.SliceSyntax@L36204`
- `grammar.RangeSyntax@L36773`, `req.RangeTypeParserOwnership@L36797`, `grammar.RecordSyntax@L37850`, `grammar.EnumSyntax@L38825`, `req.EnumVariantSeparatorSyntax@L38850`, `req.EnumTopLevelCommaSeparatorRejected@L39082`, `grammar.UnionTypeSyntax@L39935`, `grammar.TypeAliasSyntax@L40557`

#### `spec.grammar`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L98736-L99400.

- `grammar.B.1.LexicalGrammar@L98736`, `grammar.B.2.TypeGrammar@L98786`, `req.B.2.ClosureTypeUnionParameterParentheses@L98846`, `grammar.B.2.GenericRefinementModalTypeGrammar@L98859`, `grammar.B.3.ExpressionGrammar@L98892`, `req.B.3.ClosureExprUnionParameterParentheses@L98970`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L98983`, `grammar.B.4.PatternGrammar@L99017`
- `grammar.B.5.StatementGrammar@L99047`, `grammar.B.6.DeclarationGrammar@L99084`, `grammar.B.7.ContractGrammar@L99168`, `grammar.B.8.AttributeGrammar@L99195`, `grammar.B.9.KeySystemGrammar@L99248`, `grammar.B.10.ConcurrencyGrammar@L99278`, `grammar.B.11.AsyncGrammar@L99312`, `grammar.B.12.MetaprogrammingGrammar@L99341`
- `grammar.B.13.FFIGrammar@L99374`, `grammar.B.14.RegionGrammar@L99400`

### Authority, Abstract Machine, And Runtime State

#### `authority.model`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L12070-L12070.

- `conformance.NoAmbientAuthorityDiscipline@L12070`

#### `authority.capabilities`

Count: 8 total; 7 required; 0 recommended; 0 informative. Ledger line span: L12086-L12193.

- `def.CapToken@L12086`, `def.CapInTypeSignature@L12100`, `def.CapInTypeRootMappings@L12114`, `def.CapInTypePermission@L12134`, `def.CapInTypeStructuralCollections@L12148`, `def.CapInTypeStructuralNominal@L12164`, `req.CapInTypeLeastFixedPointAllowed@L12179`, `req.CapInTypeCyclesTerminate@L12193`

#### `authority.no-ambient-authority`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L12209-L12270.

- `NAA-1@L12209`, `NAA-2@L12224`, `NAA-3@L12239`, `def.CapReq@L12256`, `req.DirectCallCapabilityInclusion@L12270`

#### `authority.attenuation`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L12286-L12365.

- `def.AttenuationOperations@L12286`, `req.AttenuationMonotoneAuthority@L12307`, `req.AttenuationChildLiveness@L12321`, `req.AttenuationChildDropDoesNotDiminishParent@L12337`, `req.AttenuationParentDropRejectedWithLiveChildren@L12351`, `req.AttenuationDelegationSubsetRouting@L12365`

#### `abstract-machine.observable-behavior`

Count: 5 total; 5 required; 0 recommended; 0 informative. Ledger line span: L12381-L12454.

- `def.ObservableEffect@L12381`, `def.ObservableEvent@L12401`, `def.ObservableBehavior@L12420`, `req.AsIfObservablePreservation@L12434`, `req.AsIfForbiddenObservableChanges@L12454`

#### `abstract-machine.sequence-points`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L12475-L12513.

- `def.SequencePoint@L12475`, `def.CanonicalSequencePoints@L12492`, `req.SequencePointLTRPreservation@L12513`

#### `authority.unsafe-ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L12529-L12529.

- `conformance.UnsafeForeignAuthority@L12529`

#### `runtime.host-primitives`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L12545-L12636.

- `def.FilesystemHostPrimitiveSets@L12545`, `def.NonFilesystemHostPrimitiveSets@L12561`, `def.HostPrimitiveUniverse@L12579`, `def.DiagnosticHostPrimitives@L12593`, `def.RuntimeHostPrimitives@L12607`, `def.HostPrimitiveMappingAndFailure@L12621`, `req.UnmappedHostPrimitiveFailureIllFormed@L12636`

#### `runtime.filesystem-primitives`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L12652-L13561.

- `conformance.FilesystemPrimitiveOwnership@L12652`, `def.FilesystemPrimitiveJudgments@L12666`, `def.FilesystemPrimitiveResultTypes@L12680`, `def.FilesystemStateTypes@L12716`, `def.FilesystemStateProjections@L12732`, `def.FilesystemEntryQueries@L12750`, `def.FileHandleQueries@L12770`, `def.DirectoryIteratorQueries@L12795`
- `def.FileFlushedQuery@L12821`, `def.FilesystemExplicitStateJudgments@L12835`, `def.FilesystemStateElidedEquivalences@L12851`, `def.RestrictPathSuccess@L12886`, `def.RestrictPathFailure@L12900`, `def.FilesystemPathOperationSet@L12914`, `FSRestrict-Delegate@L12928`, `FSRestrict-InvalidPath@L12943`
- `FSRestrict-ExistsInvalidFalse@L12958`, `def.FilesystemPathErrorOperationSets@L12973`, `def.FilesystemPathErrorPredicates@L12989`, `FSPath-InvalidPath-NoData@L13007`, `FSPath-InvalidPath-Data@L13022`, `FSPath-NotFound@L13037`, `FSPath-PermissionDenied@L13052`, `FSPath-CreateWriteAlreadyExists@L13067`
- `FSPath-CreateDirAlreadyExistsNonDir@L13082`, `FSPath-OpenDirInvalidNonDir@L13097`, `FSPath-Busy@L13112`, `FSPath-OtherFailure@L13127`, `FSReadFile-InvalidUtf8@L13142`, `FileReadAll-InvalidUtf8@L13157`, `FSExists-True@L13172`, `FSExists-False@L13187`
- `def.FileHandleStateAndMode@L13202`, `def.FileLengthAndByteLength@L13221`, `def.DirectoryEntryOrdering@L13241`, `FSOpenRead-HandleState@L13261`, `FSOpenWrite-HandleState@L13276`, `FSOpenAppend-HandleState@L13291`, `FSCreateWrite-HandleState@L13306`, `FSReadFile-DelegatesThroughHandle@L13321`
- `FSReadBytes-DelegatesThroughHandle@L13336`, `FileReadAll-ClosedFailure@L13351`, `FileReadAllBytes-ClosedFailure@L13366`, `FileWrite-ClosedFailure@L13381`, `FileFlush-ClosedFailure@L13396`, `FileReadAll-AdvancesToEnd@L13411`, `FileReadAllBytes-AdvancesToEnd@L13426`, `FileWrite-PositionUpdate@L13441`
- `FileWrite-LengthUpdate@L13456`, `FileFlush-RecordsFlushed@L13471`, `FileClose-ClosesHandle@L13486`, `FSOpenDir-IteratorState@L13501`, `DirNext-ClosedFailure@L13516`, `DirNext-Exhausted@L13531`, `DirNext-YieldsEntry@L13546`, `DirClose-ClosesIterator@L13561`

#### `runtime.system-primitives`

Count: 8 total; 8 required; 0 recommended; 0 informative. Ledger line span: L13578-L13694.

- `def.SystemState@L13578`, `def.SystemPrimitiveJudgments@L13595`, `def.SystemPrimitiveStateElidedEquivalences@L13610`, `def.EmptyStringVal@L13626`, `System-GetEnv-Ok@L13640`, `System-GetEnv-None@L13658`, `System-Exit@L13676`, `System-Run@L13694`

#### `runtime.network-primitives`

Count: 6 total; 6 required; 0 recommended; 0 informative. Ledger line span: L13714-L13785.

- `def.NetworkPrimitiveJudgments@L13714`, `req.NetRestrictHostRequiredSemantics@L13728`, `req.NetRestrictHostSubsetAuthority@L13742`, `req.NetRestrictHostEqualityEnforcement@L13757`, `req.NetRestrictRejectBeforeObservableEffect@L13771`, `req.NetRestrictNoUnrelatedMutation@L13785`

#### `runtime.primitive-method-application`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L13801-L14407.

- `def.PrimitiveReceiverHandleHelpers@L13801`, `def.PrimitiveMethodNameAndOwner@L13816`, `def.PrimCallJudgment@L13835`, `Prim-FS-OpenRead@L13849`, `Prim-FS-OpenWrite@L13867`, `Prim-FS-OpenAppend@L13885`, `Prim-FS-CreateWrite@L13903`, `Prim-FS-ReadFile@L13921`
- `Prim-FS-ReadBytes@L13939`, `Prim-FS-WriteFile@L13957`, `Prim-FS-WriteStdout@L13975`, `Prim-FS-WriteStderr@L13993`, `Prim-FS-Exists@L14011`, `Prim-FS-Remove@L14029`, `Prim-FS-OpenDir@L14047`, `Prim-FS-CreateDir@L14065`
- `Prim-FS-EnsureDir@L14083`, `Prim-FS-Kind@L14101`, `Prim-FS-Restrict@L14119`, `Prim-File-ReadAll@L14137`, `Prim-File-ReadAllBytes@L14155`, `Prim-File-Write@L14173`, `Prim-File-Flush@L14191`, `Prim-File-Write-Append@L14209`
- `Prim-File-Flush-Append@L14227`, `Prim-File-Close-Read@L14245`, `Prim-File-Close-Write@L14263`, `Prim-File-Close-Append@L14281`, `Prim-Dir-Next@L14299`, `Prim-Dir-Close@L14317`, `Prim-System-GetEnv@L14335`, `Prim-System-Exit@L14353`
- `Prim-System-Run@L14371`, `Prim-Network-RestrictHost@L14389`, `req.PrimSystemExitAbortOutcome@L14407`

#### `runtime.binding-store`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L16858-L17386.

- `def.ScopeEntry@L16858`, `def.DynamicScopeStack@L16877`, `def.UpdateScopeStack@L16895`, `def.RuntimeScopePushPop@L16909`, `def.AppendCleanup@L16924`, `def.CleanupList@L16938`, `def.ScopeById@L16952`, `def.ReplaceScopeById@L16969`
- `def.SetCleanupList@L16986`, `def.PoisonedModule@L17000`, `req.HostedSessionPoisonFlagLocalization@L17014`, `def.PoisonedModules@L17028`, `def.RuntimeBindingIdentityAndValue@L17042`, `def.FreshBindId@L17057`, `def.Last@L17071`, `def.NearestScope@L17086`
- `def.LookupBind@L17103`, `def.RuntimeBindingValueLookup@L17117`, `def.RuntimeBindingStateLookup@L17131`, `LookupVal-Bind-Value@L17145`, `LookupVal-Bind-Alias@L17163`, `LookupVal-Path@L17181`, `LookupValPath-Builtin@L17199`, `LookupValPath-Static@L17217`
- `LookupValPath-Proc@L17235`, `LookupValPath-RecordCtor@L17253`, `def.ScopeValueAndStateUpdate@L17271`, `def.UpdateRuntimeBindingValue@L17286`, `def.SetRuntimeBindingState@L17300`, `def.RuntimeBindingTypeAndInfo@L17314`, `def.BindRuntimeValue@L17329`, `def.BindPatternValue@L17343`
- `def.PatternBindingOrder@L17357`, `def.BindRuntimeList@L17371`, `def.BindPattern@L17386`

#### `runtime.regions`

Count: 45 total; 45 required; 0 recommended; 0 informative. Ledger line span: L17402-L18064.

- `def.RuntimeRegionEntry@L17402`, `def.RuntimeAddressTags@L17420`, `def.RuntimeRegionStack@L17435`, `def.RegionArena@L17449`, `def.UpdateRegionArena@L17464`, `def.ArenaNew@L17478`, `def.FreshRuntimeAddress@L17492`, `def.Prefix@L17506`
- `def.ArenaAppend@L17520`, `def.ArenaMark@L17534`, `def.ArenaResetTo@L17548`, `def.ArenaClear@L17562`, `def.ArenaRemove@L17576`, `def.RegionValue@L17590`, `def.ResolveRuntimeRegionEntry@L17605`, `def.ActiveRuntimeRegion@L17622`
- `def.ResolveRuntimeRegionTargetAndTag@L17637`, `def.FreshRuntimeRegionTagAndArena@L17652`, `def.UpdateRegionStack@L17667`, `def.RegionNew@L17681`, `def.RegionOpen@L17695`, `def.FrameEnter@L17709`, `def.BindRegionAlias@L17723`, `def.TagAddr@L17738`
- `def.TagAddrFrom@L17752`, `def.RegionAlloc@L17766`, `def.FreshRuntimeRegionTags@L17780`, `def.RetagRegions@L17794`, `def.RegionReset@L17811`, `def.PopRegions@L17825`, `def.RegionFree@L17842`, `def.FrameMark@L17856`
- `def.PopRegionScope@L17870`, `def.ReleaseArena@L17887`, `def.ResetArena@L17901`, `req.RegionRuntimeOwnershipBoundary@L17915`, `req.RegionReleaseCleanupBeforeArenaReclaim@L17931`, `req.ArenaReclaimNoDrop@L17946`, `def.RegionProcedureJudgements@L17960`, `Region-New-Scoped@L17974`
- `Region-Alloc-Proc@L17992`, `Region-Reset-Proc@L18010`, `Region-Freeze-Proc@L18028`, `Region-Thaw-Proc@L18046`, `Region-Free-Proc@L18064`

#### `runtime.value-model`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L18082-L18347.

- `def.BlockEnter@L18082`, `def.ScalarRuntimeValues@L18096`, `def.PointerRuntimeValues@L18118`, `def.AggregateRuntimeValues@L18132`, `def.RuntimeValueDomain@L18155`, `def.TupleValueOperations@L18169`, `def.RecordFieldValueOperations@L18184`, `def.IndexAndSliceValueOperations@L18199`
- `def.AddressPrimitiveJudgments@L18217`, `def.AddressArithmetic@L18231`, `def.ElementType@L18245`, `def.AggregateAddressCalculation@L18259`, `def.PointerStateAndAddress@L18278`, `def.BindingAddresses@L18297`, `def.RuntimeAddressTagLookup@L18315`, `def.RuntimeTagActive@L18332`
- `def.DynamicAddressState@L18347`

#### `runtime.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27083-L27292.

- `conformance.AttributeDynamicSemantics@L27083`, `conformance.VendorAttributeDynamicSemantics@L27292`

#### `runtime.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27579-L27579.

- `conformance.LayoutAttributeDynamicSemantics@L27579`

#### `runtime.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27724-L27724.

- `conformance.OptimizationAttributeDynamicSemantics@L27724`

#### `runtime.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28203-L28203.

- `conformance.DiagnosticsMetadataDynamicSemantics@L28203`

#### `runtime.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28451-L28670.

- `conformance.PermissionDynamicSemantics@L28451`, `conformance.AliasExclusivityDynamicSemantics@L28670`

#### `runtime`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L29083-L40526.

- `conformance.PermissionAdmissibilityRuntimeIdentity@L29083`, `conformance.ImportDeclarationDynamicSemantics@L29332`, `conformance.UsingDeclarationDynamicSemantics@L29839`, `conformance.StaticDeclarationDynamicSemantics@L30132`, `req.HostedLibraryStaticAddrSessionLocal@L30426`, `conformance.ExternBlockDynamicSemantics@L31030`, `conformance.ModuleAggregationDynamicSemantics@L34227`, `def.PrimitiveValueTypes@L34553`
- `req.PrimitiveOperationEvaluationOwnership@L34576`, `def.TupleValueType@L35257`, `def.ArrayValueType@L35978`, `def.ArrayIndexRuntimeHelpers@L35994`, `def.SliceValueType@L36588`, `def.SliceRuntimeIndexHelpers@L36604`, `def.SliceIndexUpdate@L36722`, `def.RangeValueTypes@L37247`
- `def.SliceBoundsRaw@L37336`, `def.SliceBounds@L37355`, `def.RecordValueType@L38444`, `def.RecordDefaultInits@L38496`, `def.EnumValueType@L39619`, `def.UnionCase@L40177`, `def.UnionValueType@L40526`

### Modules, Name Resolution, And Visibility

#### `checker.name-resolution`

Count: 307 total; 307 required; 0 recommended; 0 informative. Ledger line span: L18399-L23682.

- `def.ScopeKeyConstraint@L18399`, `def.GlobalResolutionTables@L18414`, `def.ResolutionContext@L18431`, `def.ResolutionEntity@L18449`, `def.ResolutionScope@L18467`, `def.ResolutionScopeStack@L18481`, `def.UniverseBindings@L18499`, `def.BytePrefix@L18514`
- `def.ReservedIdentifiers@L18529`, `def.ReservedModulePath@L18545`, `def.BuiltinTypeNameSets@L18561`, `req.PredicateNameUniverseReservation@L18577`, `def.NameResolutionKeywordKeys@L18591`, `def.ScopeDomain@L18611`, `def.NameIntroductionScopeSequence@L18625`, `def.InScope@L18639`
- `def.InOuter@L18653`, `Intro-Ok@L18667`, `Intro-Dup@L18685`, `Intro-Outer-Err@L18703`, `Intro-Reserved-Gen-Err@L18721`, `Intro-Reserved-Ultraviolet-Err@L18739`, `req.IntroRulePriority@L18757`, `def.UsingAlias@L18773`
- `Using-Alias-Ok@L18789`, `Using-Alias-Unresolved@L18807`, `Using-Alias-Dup@L18825`, `Using-Alias-Reserved@L18843`, `req.UsingAliasRulePriority@L18861`, `def.ModuleNameValidationHelpers@L18875`, `Validate-Module-Ok@L18889`, `Validate-Module-Keyword-Err@L18907`
- `req.UniverseScopeReuseHandledByIntro@L18925`, `def.LookupScopeSequence@L18941`, `def.LookupNearestScopeIndex@L18955`, `Lookup-Unqualified@L18969`, `Lookup-Unqualified-None@L18987`, `def.EntityKindPredicates@L19005`, `def.RegionAliasName@L19023`, `Resolve-Value-Name@L19037`
- `Resolve-Type-Name@L19055`, `Resolve-Class-Name@L19073`, `Resolve-Module-Name@L19091`, `def.QualifiedResolutionProjectInput@L19109`, `def.QualifiedResolutionCurrentModule@L19123`, `def.QualifiedResolutionVisibleModules@L19137`, `def.QualifiedResolutionAliasMap@L19152`, `def.QualifiedResolutionKindDomain@L19166`
- `req.ModuleVisibilityJudgementOwnership@L19180`, `req.ResolveModulePathCanonicalOwner@L19194`, `Resolve-Qualified@L19208`, `prop.CollectNamesOrderIndependence@L19464`, `def.BindingKindDomain@L19478`, `def.BindingSourceDomain@L19492`, `def.NameInfoShape@L19506`, `def.ModuleNameMap@L19520`
- `def.AliasMap@L19534`, `def.UsingMap@L19548`, `def.UsingValueMap@L19562`, `def.UsingTypeMap@L19576`, `def.TypeMap@L19590`, `def.ClassMap@L19604`, `PatNames-IdentifierPattern@L19618`, `PatNames-WildcardPattern@L19633`
- `PatNames-LiteralPattern@L19648`, `PatNames-TuplePattern@L19663`, `PatNames-RecordFieldPattern@L19680`, `PatNames-RecordFieldShorthand@L19697`, `PatNames-RecordPattern@L19712`, `PatNames-EnumNoPayload@L19729`, `PatNames-EnumTuplePayload@L19744`, `PatNames-EnumRecordPayload@L19761`
- `PatNames-RangePattern@L19778`, `def.AllModuleNames@L19795`, `def.VisibleModuleNames@L19809`, `def.LastPathComponent@L19823`, `def.IsModulePath@L19837`, `def.SplitLastPathComponent@L19851`, `def.ModuleByPath@L19865`, `def.ItemNames@L19879`
- `def.UsingSpecName@L19893`, `def.UsingSpecNames@L19909`, `DeclNames-Empty@L19923`, `DeclNames-Using@L19938`, `DeclNames-Item@L19956`, `def.ModuleDeclNames@L19974`, `req.ImportUsingJudgementCanonicalOwner@L19988`, `Bind-Procedure@L20002`
- `Bind-ExternBlock@L20019`, `Bind-Record@L20037`, `Bind-Enum@L20054`, `Bind-Class@L20071`, `Bind-TypeAlias@L20088`, `Bind-Static@L20105`, `Bind-Import@L20123`, `Bind-Import-Err@L20141`
- `Bind-Using@L20159`, `Bind-Using-Err@L20177`, `Bind-ErrorItem@L20195`, `Collect-Ok@L20212`, `Collect-Scan@L20230`, `Collect-Using-Import-Dup@L20248`, `Collect-Dup@L20266`, `Collect-Err@L20284`
- `def.BindingNameSet@L20302`, `def.NoDuplicateBindingNames@L20316`, `def.DisjointBindingNames@L20330`, `def.NameMapUnion@L20344`, `def.NameInfoOfBinding@L20358`, `def.BindingNameSource@L20372`, `def.NameMapSource@L20386`, `def.UsingImportConflict@L20400`
- `def.NameCollectionStateDomain@L20414`, `Names-Start@L20428`, `Names-Step@L20445`, `Names-Step-Using-Import-Dup@L20463`, `Names-Step-Dup@L20481`, `Names-Step-Err@L20499`, `Names-Done@L20517`, `def.ResolveQualifiedFormSignature@L20536`
- `def.ResolveArgsSignature@L20550`, `def.ResolveFieldInitsSignature@L20564`, `def.ResolveRecordPathSignature@L20578`, `def.ResolveEnumUnitSignature@L20592`, `def.ResolveEnumTupleSignature@L20606`, `def.ResolveEnumRecordSignature@L20620`, `def.ResolvePathJudgementSet@L20634`, `ResolveArgs-Empty@L20648`
- `ResolveArgs-Cons@L20665`, `ResolveFieldInits-Empty@L20683`, `ResolveFieldInits-Cons@L20700`, `Resolve-RecordPath@L20718`, `Resolve-EnumUnit@L20736`, `Resolve-EnumTuple@L20754`, `Resolve-EnumRecord@L20772`, `def.BuiltinValuePath@L20790`
- `ResolveQual-Name-Builtin@L20804`, `ResolveQual-Name-Value@L20822`, `ResolveQual-Name-Record@L20840`, `ResolveQual-Name-Enum@L20858`, `ResolveQual-Name-Err@L20876`, `def.SharedResolutionProjectInput@L20897`, `def.SharedResolutionCurrentModule@L20911`, `def.SharedResolutionAstModule@L20925`
- `def.SharedResolutionInputs@L20939`, `def.SharedResolutionOutputs@L20953`, `def.PathOfModuleReference@L20967`, `def.TypeParamBindings@L20981`, `ResolveGenericParamsOpt-None@L20996`, `ResolvePredicateClauseOpt-None@L21011`, `ResolveContractClauseOpt-None@L21026`, `ResolveInvariantOpt-None@L21041`
- `ResolveTypeOpt-None@L21056`, `ResolveExprOpt-None-Judgement@L21071`, `def.ResolveExprOptNone@L21086`, `def.ResolveExprOptSome@L21100`, `ResolveGenericParamsOpt-Yes@L21114`, `ResolveTypeParam@L21132`, `ResolveTypeParamList-Empty@L21150`, `ResolveTypeParamList-Cons@L21165`
- `ResolvePredicateClauseOpt-Yes@L21183`, `ResolvePredicateReqList-Empty@L21201`, `ResolvePredicateReq-Predicate@L21216`, `ResolvePredicateReqList-Cons@L21234`, `ResolveContractClauseOpt-Yes@L21252`, `ResolveInvariantOpt-Yes@L21270`, `ResolveTypePath-Ident@L21288`, `ResolveTypePath-Ident-Local@L21306`
- `ResolveTypePath-Qual@L21324`, `def.LocalTypePath@L21342`, `ResolveClassPath-Ident@L21356`, `ResolveClassPath-Qual@L21374`, `ResolveType-Path@L21392`, `ResolveType-Dynamic@L21410`, `ResolveType-Apply@L21428`, `ResolveType-ModalState@L21446`
- `def.ResolveModalRef@L21464`, `ResolveType-Hom@L21480`, `ResolveTypeList-Empty@L21498`, `ResolveTypeList-Cons@L21513`, `ResolveParam@L21531`, `ResolveParams-Empty@L21549`, `ResolveParams-Cons@L21564`, `def.ResolvePatternSignature@L21583`
- `ResolvePat-Wildcard@L21597`, `ResolvePat-Identifier@L21612`, `ResolvePat-Literal@L21627`, `ResolvePat-Tuple@L21642`, `ResolvePat-Record@L21660`, `ResolvePat-Enum@L21678`, `ResolvePat-Modal@L21696`, `ResolvePat-Range@L21714`
- `ResolvePatternList-Empty@L21732`, `ResolveFieldPatternList-Empty@L21747`, `ResolvePatternList-Cons@L21762`, `ResolveFieldPattern-Implicit@L21780`, `ResolveFieldPattern-Explicit@L21797`, `ResolveFieldPatternList-Cons@L21815`, `ResolveEnumPayloadPattern-None@L21833`, `ResolveEnumPayloadPattern-Tuple@L21848`
- `ResolveEnumPayloadPattern-Record@L21866`, `ResolveFieldPatternListOpt-None@L21884`, `ResolveFieldPatternListOpt-Some@L21899`, `ResolveExpr-Ident@L21918`, `ResolveExpr-Ident-Err@L21936`, `ResolveExpr-Qualified@L21954`, `def.ResolveArgsReference@L21972`, `def.ResolveFieldInitsReference@L21986`
- `ResolveExprList-Empty@L22000`, `ResolveExprList-Cons@L22015`, `def.ResolveExprListJudgementSet@L22033`, `def.ResolveEnumPayloadJudgementSet@L22047`, `ResolveEnumPayload-None@L22061`, `ResolveEnumPayload-Tuple@L22078`, `ResolveEnumPayload-Record@L22096`, `def.ResolveKeyPathJudgementSet@L22114`
- `ResolveKeySeg-Field@L22128`, `ResolveKeySeg-Index@L22146`, `ResolveKeySegs-Empty@L22164`, `ResolveKeySegs-Cons@L22181`, `ResolveKeyPathExpr@L22199`, `ResolveKeyPathExpr-Err@L22217`, `ResolveKeyPathList-Empty@L22235`, `ResolveKeyPathList-Cons@L22252`
- `def.ResolveParallelOptJudgementSet@L22270`, `ResolveParallelOpt-Cancel@L22284`, `ResolveParallelOpt-Name@L22302`, `ResolveParallelOpts-Empty@L22319`, `ResolveParallelOpts-Cons@L22336`, `def.ResolveSpawnOptJudgementSet@L22354`, `ResolveSpawnOpt-Name@L22368`, `ResolveSpawnOpt-Affinity@L22385`
- `ResolveSpawnOpt-Priority@L22403`, `ResolveSpawnOpts-Empty@L22421`, `ResolveSpawnOpts-Cons@L22438`, `def.ResolveDispatchOptJudgementSet@L22456`, `ResolveDispatchOpt-Reduce@L22470`, `ResolveDispatchOpt-Ordered@L22487`, `ResolveDispatchOpt-Chunk@L22504`, `ResolveDispatchOpts-Empty@L22522`
- `ResolveDispatchOpts-Cons@L22539`, `def.ResolveRaceJudgementSet@L22557`, `ResolveRaceHandler-Return@L22571`, `ResolveRaceHandler-Yield@L22589`, `ResolveRaceArm@L22607`, `ResolveRaceArms-Empty@L22625`, `ResolveRaceArms-Cons@L22642`, `def.ResolveAllExprListJudgementSet@L22660`
- `ResolveAllExprList-Empty@L22674`, `ResolveAllExprList-Cons@L22691`, `def.ResolveCalleeJudgementSet@L22709`, `ResolveCallee-Ident-Value@L22723`, `ResolveCallee-Ident-Record@L22741`, `ResolveCallee-Path-Value@L22759`, `ResolveCallee-Path-Builtin@L22777`, `ResolveCallee-Path-Record@L22795`
- `ResolveCallee-Other@L22813`, `ResolveExpr-Call@L22831`, `ResolveExpr-Call-TypeArgs@L22849`, `ResolveExpr-RecordExpr@L22868`, `ResolveExpr-EnumLiteral@L22886`, `def.ResolveIfCaseJudgementSet@L22904`, `ResolveIfCase@L22918`, `ResolveIfCases-Empty@L22936`
- `ResolveIfCases-Cons@L22953`, `ResolveElseBlockOpt-None@L22971`, `ResolveElseBlockOpt-Some@L22988`, `ResolveExpr-IfIs@L23006`, `ResolveExpr-IfCase@L23024`, `ResolveExpr-LoopInfinite@L23042`, `ResolveExpr-LoopConditional@L23060`, `ResolveExpr-LoopIter@L23078`
- `ResolveExpr-Parallel@L23096`, `ResolveExpr-Spawn@L23114`, `ResolveExpr-Wait@L23132`, `def.ResolveKeyClauseJudgementSet@L23150`, `ResolveKeyClauseOpt-None@L23164`, `ResolveKeyClauseOpt-Yes@L23182`, `ResolveExpr-Dispatch@L23200`, `ResolveExpr-Yield@L23218`
- `ResolveExpr-YieldFrom@L23236`, `ResolveExpr-Sync@L23254`, `ResolveExpr-Race@L23272`, `ResolveExpr-All@L23290`, `ResolveExpr-Alloc-Explicit-ByAlias@L23308`, `def.ResolveExprRuleSet@L23326`, `def.NoSpecificResolveExpr@L23340`, `ResolveExpr-Hom@L23354`
- `ResolveExpr-Alloc-Implicit@L23372`, `ResolveExpr-Alloc-Explicit@L23390`, `def.ResolveStmtSeqJudgementSet@L23408`, `ResolveStmtSeq-Empty@L23422`, `ResolveStmtSeq-Cons@L23439`, `ResolveExpr-Block@L23457`, `Validate-ModulePath-Ok@L23476`, `Validate-ModulePath-Reserved-Err@L23494`
- `req.ResolveItemFeatureOwnership@L23512`, `ResolveModule-Ok@L23526`, `ResolveItems-Empty@L23544`, `ResolveItems-Cons@L23559`, `def.ResolutionStateDomain@L23577`, `Res-Start@L23591`, `Res-Names@L23608`, `Res-Items@L23626`
- `ResolveModules-Ok@L23646`, `ResolveModules-Err-Parse@L23664`, `ResolveModules-Err-Resolve@L23682`

#### `checker.visibility`

Count: 15 total; 15 required; 0 recommended; 0 informative. Ledger line span: L19228-L19444.

- `def.DeclOfModuleItem@L19228`, `def.DeclOfExternProc@L19242`, `def.ModuleOfItem@L19256`, `def.ModuleOfExternProc@L19270`, `def.ExternBlockOfProc@L19284`, `def.ExternProcName@L19298`, `def.VisibilityOfDeclaration@L19312`, `def.SameAssembly@L19326`
- `Access-Public@L19340`, `Access-Internal@L19358`, `Access-Private@L19376`, `Access-Internal-Err@L19394`, `Access-Err@L19412`, `def.TopLevelDeclarationPredicate@L19430`, `TopLevelVis-Ok@L19444`

#### `checker.modules`

Count: 209 total; 209 required; 0 recommended; 0 informative. Ledger line span: L29191-L34273.

- `Parse-Import@L29191`, `def.ImportDeclAst@L29209`, `req.ImportDeclarationBindingSemanticsScope@L29227`, `Import-Path@L29243`, `Import-Path-Err@L29261`, `Bind-Import@L29279`, `Bind-Import-Err@L29297`, `ResolveItem-Import@L29315`
- `diagnostics.ImportDeclarations@L29364`, `req.ImportDeclarationDiagnosticOwnership@L29382`, `Parse-Using-Wildcard@L29426`, `Parse-Using-List@L29444`, `Parse-Using-Item@L29462`, `Parse-UsingSpec@L29480`, `Parse-UsingList-Empty@L29498`, `Parse-UsingList-Cons@L29516`
- `Parse-UsingListTail-End@L29534`, `Parse-UsingListTail-TrailingComma@L29552`, `Parse-UsingListTail-Comma@L29570`, `def.UsingDeclAst@L29588`, `req.UsingDeclarationBindingSemanticsScope@L29614`, `def.UsingSpecName@L29630`, `def.UsingSpecNames@L29646`, `Using-Item@L29660`
- `Using-Item-Public-Err@L29678`, `Using-List@L29696`, `Using-Wildcard-Warn@L29714`, `Using-Wildcard@L29732`, `Using-List-Dup@L29750`, `Using-List-Public-Err@L29768`, `Bind-Using@L29786`, `Bind-Using-Err@L29804`
- `ResolveItem-Using@L29822`, `diagnostics.UsingDeclarations@L29871`, `req.UsingDeclarationDiagnosticOwnership@L29892`, `def.StaticDeclTopLevelItems@L29928`, `Parse-Static-Decl@L29958`, `def.StaticDeclAst@L29976`, `req.StaticDeclModuleScopeBindingSemantics@L29994`, `def.StaticVisOk@L30010`
- `Bind-Static@L30024`, `WF-StaticDecl@L30042`, `WF-StaticDecl-Ann-Mismatch@L30060`, `WF-StaticDecl-MissingType@L30078`, `StaticVisOk-Err@L30096`, `ResolveItem-Static@L30114`, `def.StaticBindTypes@L30194`, `def.StaticBindList@L30208`
- `Emit-Static-Const@L30252`, `Emit-Static-Init@L30270`, `Emit-Static-Multi@L30288`, `InitFn@L30320`, `DeinitFn@L30352`, `def.StaticItems@L30370`, `def.StaticItemOf@L30384`, `def.StaticType@L30454`
- `def.StaticBindInfo@L30468`, `Lower-StaticInit-Item@L30512`, `Lower-StaticInitItems-Empty@L30530`, `Lower-StaticInitItems-Cons@L30547`, `Lower-StaticInit@L30565`, `InitCallIR@L30583`, `Lower-StaticDeinitNames-Empty@L30616`, `Lower-StaticDeinitNames-Cons-Resp@L30633`
- `Lower-StaticDeinitNames-Cons-NoResp@L30651`, `Lower-StaticDeinit-Item@L30669`, `Lower-StaticDeinitItems-Empty@L30687`, `Lower-StaticDeinitItems-Cons@L30704`, `Lower-StaticDeinit@L30722`, `diagnostics.StaticDeclarations@L30740`, `req.StaticDeclarationDiagnosticOwnership@L30760`, `Parse-ExternBlock@L30813`
- `Parse-ExternAbiOpt-None@L30831`, `Parse-ExternAbiOpt-String@L30849`, `Parse-ExternAbiOpt-Ident@L30867`, `Parse-ExternItemList-End@L30885`, `Parse-ExternItemList-Cons@L30903`, `def.ExternBlockAst@L30921`, `req.ExternBlockStaticSemanticsScope@L30942`, `Bind-ExternBlock@L30958`
- `WF-ExternBlock@L30976`, `ExternAbi-Unknown-Err@L30994`, `ResolveItem-ExternBlock@L31012`, `req.ModuleAggregationStaticSemanticsScope@L31242`, `def.NameCollectAfterParse@L31504`, `def.QualifiedLookupContext@L32093`, `def.ModuleAssemblyPathHelpers@L32110`, `def.ImportDeclarationsOfModule@L32126`
- `def.VisibleModulesAndNames@L32141`, `def.PathPrefix@L32163`, `AliasExpand-None@L32179`, `AliasExpand-Yes@L32197`, `def.CurrentAsmPath@L32215`, `ModulePrefix-Direct@L32229`, `ModulePrefix-Current@L32247`, `ModulePrefix-None@L32265`
- `Resolve-ModulePath-Direct@L32283`, `Resolve-ModulePath-Current@L32301`, `ResolveModulePath-Err@L32319`, `def.ModuleByPath@L32337`, `def.ModuleOfPath@L32351`, `def.ItemNames@L32365`, `ItemOfPath@L32379`, `ItemOfPath-None@L32397`
- `def.ImportCoveragePredicates@L32415`, `def.ImportOkJudgementSet@L32430`, `Import-Ok-Local@L32444`, `Import-Ok-Covered@L32462`, `Import-Ok-Err@L32480`, `Resolve-Import-Direct@L32498`, `Resolve-Import-Current@L32516`, `Resolve-Import-Err@L32534`
- `Resolve-Using-Ok@L32552`, `Resolve-Using-Err@L32570`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32588`, `def.ModuleInitializationDependencyEnvironment@L32603`, `Reachable-Edge@L32619`, `Reachable-Step@L32637`, `def.ModuleInitializationPathHelpers@L32655`, `def.TypeRefsJudgementSet@L32671`
- `def.TypeReferenceEnvironmentAliases@L32685`, `TypeRef-Path@L32701`, `TypeRef-Using@L32719`, `TypeRef-Path-Local@L32737`, `TypeRef-Dynamic@L32755`, `TypeRef-ModalState@L32773`, `TypeRef-Apply@L32791`, `TypeRef-Perm@L32809`
- `TypeRef-Prim@L32827`, `TypeRef-Tuple@L32844`, `TypeRef-Array@L32862`, `TypeRef-Slice@L32880`, `TypeRef-Union@L32898`, `TypeRef-Func@L32916`, `TypeRef-String@L32934`, `TypeRef-Bytes@L32951`
- `TypeRef-Ptr@L32968`, `TypeRef-RawPtr@L32986`, `TypeRef-Range@L33004`, `TypeRef-RangeInclusive@L33022`, `TypeRef-RangeFrom@L33040`, `TypeRef-RangeTo@L33058`, `TypeRef-RangeToInclusive@L33076`, `TypeRef-RangeFull@L33094`
- `TypeRef-Ref-Path@L33111`, `TypeRef-Ref-Apply@L33129`, `TypeRef-Ref-ModalState@L33147`, `TypeRef-RecordExpr@L33165`, `TypeRef-EnumLiteral@L33183`, `TypeRef-QualBrace@L33201`, `TypeRef-Cast@L33219`, `TypeRef-Transmute@L33237`
- `TypeRef-CallTypeArgs@L33255`, `def.TypeRefsExprRules@L33273`, `def.NoSpecificTypeRefsExpr@L33287`, `TypeRef-Expr-Sub@L33301`, `TypeRef-RecordPattern@L33319`, `TypeRef-EnumPattern@L33337`, `TypeRef-LiteralPattern@L33355`, `TypeRef-WildcardPattern@L33372`
- `TypeRef-IdentifierPattern@L33389`, `TypeRef-TuplePattern@L33406`, `TypeRef-ModalPattern-None@L33424`, `TypeRef-ModalPattern-Record@L33441`, `TypeRef-RangePattern@L33459`, `TypeRef-Field-Explicit@L33477`, `TypeRef-Field-Implicit@L33495`, `TypeRefsExprs-Empty@L33512`
- `TypeRefsExprs-Cons@L33529`, `def.TypeRefsArgsJudgementSet@L33547`, `TypeRefsArgs-Empty@L33561`, `TypeRefsArgs-Cons@L33578`, `TypeRefsEnumPayload-None@L33596`, `TypeRefsEnumPayload-Tuple@L33613`, `TypeRefsEnumPayload-Record@L33631`, `TypeRefsFields-Empty@L33649`
- `TypeRefsFields-Cons@L33666`, `TypeRefsPayload-None@L33684`, `TypeRefsPayload-Tuple@L33701`, `TypeRefsPayload-Record@L33719`, `def.ValueReferenceEnvironmentAliases@L33737`, `def.ValueRefsJudgementSet@L33751`, `ValueRef-Ident@L33765`, `ValueRef-Ident-Local@L33783`
- `ValueRef-Qual@L33801`, `ValueRef-Qual-Local@L33819`, `ValueRef-QualApply@L33837`, `ValueRef-QualApply-Local@L33855`, `ValueRef-QualApply-Brace@L33873`, `def.ValueRefsRules@L33891`, `def.NoSpecificValueRefsExpr@L33905`, `ValueRef-Expr-Sub@L33919`
- `ValueRefsArgs-Empty@L33937`, `ValueRefsArgs-Cons@L33954`, `ValueRefsFields-Empty@L33972`, `ValueRefsFields-Cons@L33989`, `def.AstTraversalNodeHelpers@L34007`, `def.EnumVariantTypeSets@L34033`, `def.GeneralTypePositionSetHelpers@L34050`, `def.RecordMemberTypeSets@L34069`
- `def.ClassItemTypeSets@L34087`, `def.DeclarationTypePositions@L34105`, `def.TypePositionExpressions@L34126`, `def.TypeDeps@L34142`, `def.ValueDependencyExpressionSets@L34156`, `def.ValueDepsEagerLazy@L34174`, `def.ModuleDependencyGraphs@L34189`, `WF-Acyclic-Eager@L34209`
- `diagnostics.ModuleAggregation@L34273`

### Types, Permissions, Declarations, And Static Semantics

#### `checker.binding-state`

Count: 72 total; 72 required; 0 recommended; 0 informative. Ledger line span: L14425-L15866.

- `def.BindingStateDomain@L14425`, `def.BindInfo@L14439`, `def.BindingEnvironment@L14456`, `def.BindingScopeStackOps@L14471`, `def.BindingLookup@L14486`, `def.BindingUpdate@L14503`, `def.BindingIntro@L14520`, `def.BindingStateJoin@L14629`
- `def.BindingStateTransitionSet@L14648`, `Trans-Move-Whole@L14662`, `Trans-Move-Field@L14680`, `Trans-Move-Field-Partial@L14698`, `Trans-Partial-To-Moved@L14716`, `Trans-Reassign@L14734`, `Trans-Moved-NoAccess@L14752`, `Trans-Partial-NoAccess@L14770`
- `Trans-Let-NoReassign@L14788`, `def.BindingInfoJoin@L14806`, `def.BindingScopeJoin@L14822`, `def.BindingEnvironmentJoin@L14838`, `def.FieldHead@L14921`, `def.FieldPathOf@L14941`, `def.PlacePath@L14960`, `def.ArgumentPassExpression@L15040`
- `def.AccessStateOk@L15058`, `def.PartialMoveStateUpdate@L15074`, `def.ExpressionTypeLookupForAccess@L15090`, `def.AccessOk@L15105`, `def.BindingMovabilityOperator@L15121`, `def.MoveExpressionPredicate@L15136`, `def.InitializationResponsibility@L15151`, `def.BindingInitializerExpression@L15168`
- `def.BindingInitializerScope@L15184`, `def.TemporaryScope@L15200`, `def.TemporaryValuePredicate@L15216`, `def.TemporaryEvaluationOrder@L15230`, `def.ControlStatementExpression@L15251`, `def.TemporaryDropOrder@L15267`, `def.OptionalExpressionList@L15282`, `def.StatementExpressions@L15297`
- `def.StatementAndBindingScopes@L15324`, `def.BlockStatements@L15341`, `def.StatementBlocks@L15355`, `def.StatementSubExpressions@L15373`, `def.StatementSubStatements@L15389`, `def.SubBlocks@L15407`, `def.MapEntries@L15423`, `def.MapUnion@L15437`
- `def.IntroduceAllBindings@L15451`, `def.BindInfoMap@L15465`, `def.EffectiveMovability@L15479`, `def.BindingNames@L15514`, `def.JoinAllBindings@L15528`, `def.ConsumeOnMove@L15590`, `def.MoveExpressionInnerPlace@L15606`, `def.BindingJudgmentSet@L15620`
- `def.StaticBindingMaps@L15634`, `def.ProcedureEntryBindingScopes@L15652`, `def.ParameterBindingMap@L15667`, `def.MethodParameterBindingMap@L15682`, `def.ParameterTypeMap@L15696`, `def.ParameterMoveAndResponsibility@L15711`, `def.InitialBindingEnvironment@L15726`, `def.BindCheck@L15754`
- `def.ProcedureBindingCheck@L15768`, `def.MethodParametersForBinding@L15782`, `def.MethodBindingCheck@L15796`, `def.ClassMethodBindingCheck@L15810`, `def.StateMethodBindingCheck@L15824`, `def.TransitionBindingCheck@L15838`, `def.BindingDiagnosticReferences@L15852`, `req.FeatureSpecificBJudgmentOwnership@L15866`

#### `checker.permission-state`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L14536-L15740.

- `def.PermissionOfType@L14536`, `def.PermissionActivityDomain@L14551`, `def.PermissionEnvironment@L14565`, `def.PermissionScopeStackOps@L14581`, `def.PermissionLookup@L14596`, `def.PermissionUpdate@L14613`, `def.PermissionStateJoin@L14856`, `def.PermissionAtScope@L14871`
- `def.PermissionScopeJoin@L14887`, `def.PermissionEnvironmentJoin@L14901`, `def.AccessPathPrefixes@L14976`, `def.AccessPathOk@L14992`, `def.SuspendUniquePath@L15006`, `def.ReactivatePermissionKeys@L15025`, `def.JoinAllPermissions@L15544`, `def.PermissionTopScopeOps@L15560`
- `def.PermissionRoots@L15576`, `def.InitialPermissionEnvironment@L15740`

#### `checker.regions`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L15494-L16037.

- `def.RegionBindingInfo@L15494`, `def.RegionOptionsFields@L15884`, `def.RegionOptionsDecl@L15901`, `def.RegionPreallocation@L15916`, `def.RegionActiveType@L15931`, `def.FreshRegion@L15945`, `def.RegionOptionsExpression@L15959`, `def.RegionBind@L15974`
- `def.InnermostActiveRegion@L15990`, `def.FrameBind@L16007`, `req.RegionSyntheticIdentifierRestriction@L16023`, `req.FrameSyntheticIdentifierRestriction@L16037`

#### `checker.provenance`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L16053-L16840.

- `def.ProvenanceTags@L16053`, `def.RegionNesting@L16067`, `def.StrictProvenanceLifetimeOrder@L16081`, `def.ProvenanceLifetimeOrder@L16095`, `def.FrameTarget@L16109`, `def.FrameTargetProvenanceOrder@L16124`, `def.ProvenanceJoin@L16139`, `def.JoinAllProvenance@L16156`
- `def.ProvenanceEnvironmentShape@L16174`, `def.ProvenanceScopeAccessors@L16192`, `def.ProvenanceScopeStackOps@L16209`, `def.ProvenanceLookup@L16224`, `def.ProvenanceIntro@L16240`, `def.ProvenanceIntroAll@L16254`, `def.ParameterProvenanceInitialization@L16269`, `def.ProvenanceRegionEntryResolution@L16284`
- `def.ProvenanceRegionAliasIntro@L16303`, `def.FreshRegionTag@L16317`, `def.AllocationRegionTagSelection@L16331`, `def.FreshRegionExpression@L16349`, `def.ProvenanceJudgmentSets@L16363`, `def.CaseBodyProvenance@L16380`, `def.CasePatternProvenanceEnvironment@L16395`, `def.CaseProvenance@L16409`
- `def.CaseElseProvenance@L16423`, `rules.ProvenanceChecking@L16438`, `P-If-Is@L16456`, `P-If-Cases@L16474`, `def.ClosureCaptureProvenance@L16494`, `def.ClosureTargetProvenance@L16508`, `def.ClosureLocalSharedCaptures@L16524`, `def.ClosureEscapeCheck@L16538`
- `P-Closure-NonCapturing@L16554`, `P-Closure-Capturing@L16572`, `P-Closure-Escape-Err@L16591`, `def.FrameProvenance@L16610`, `def.BreakProvenance@L16628`, `def.IteratorElementProvenance@L16642`, `def.InfiniteLoopProvenance@L16656`, `def.FiniteLoopProvenance@L16671`
- `def.ExtendProvenanceForPattern@L16686`, `P-Loop-Infinite@L16700`, `P-Loop-Conditional@L16718`, `P-Loop-Iter@L16736`, `def.ProvenanceEscapeHelpers@L16754`, `req.NoGeneralHeapEscapeConversion@L16768`, `def.BindingProvenance@L16782`, `def.StaticBindingProvenance@L16798`
- `def.AssignmentProvenanceEscapeCheck@L16812`, `def.ProvenanceEscapeJudgmentSet@L16826`, `req.ProvenanceEscapeCheckPurpose@L16840`

#### `checker.types`

Count: 283 total; 181 required; 0 recommended; 0 informative. Ledger line span: L23734-L40932.

- `def.TypeEquivalenceJudgementSet@L23734`, `def.ConstLenJudgementSet@L23748`, `ConstLen-Lit@L23762`, `ConstLen-Path@L23780`, `ConstLen-Err@L23798`, `def.UnionMembersEquivalence@L23816`, `T-Equiv-Prim@L23830`, `T-Equiv-Perm@L23848`
- `T-Equiv-Tuple@L23866`, `T-Equiv-Array@L23884`, `T-Equiv-Slice@L23902`, `T-Equiv-Func@L23920`, `T-Equiv-Closure@L23938`, `T-Equiv-Union@L23956`, `T-Equiv-Path@L23974`, `T-Equiv-ModalState@L23992`
- `T-Equiv-String@L24010`, `T-Equiv-Bytes@L24028`, `T-Equiv-Range@L24046`, `T-Equiv-RangeInclusive@L24064`, `T-Equiv-RangeFrom@L24082`, `T-Equiv-RangeTo@L24100`, `T-Equiv-RangeToInclusive@L24118`, `T-Equiv-RangeFull@L24136`
- `T-Equiv-Ptr@L24154`, `T-Equiv-RawPtr@L24172`, `T-Equiv-Dynamic@L24190`, `T-Equiv-Apply@L24208`, `T-Equiv-Opaque@L24226`, `T-Equiv-Refine@L24244`, `def.PredicateEquivalence@L24262`, `T-Equiv-Refine-Norm@L24276`
- `T-Equiv-Refl@L24294`, `T-Equiv-Sym@L24312`, `T-Equiv-Trans@L24330`, `def.SubtypingJudgementSet@L24351`, `req.NoIntegerNumericSubtyping@L24365`, `req.NoFloatNumericSubtyping@L24379`, `req.PermissionAdmissibilityOwnedByChapter10@L24393`, `Sub-Perm@L24407`
- `Sub-Never@L24425`, `Sub-Tuple@L24443`, `Sub-Array@L24461`, `Sub-Slice@L24479`, `Sub-Range@L24497`, `Sub-RangeInclusive@L24515`, `Sub-RangeFrom@L24533`, `Sub-RangeTo@L24551`
- `Sub-RangeToInclusive@L24569`, `Sub-RangeFull@L24587`, `Sub-Ptr-State@L24605`, `Sub-Modal-Niche@L24623`, `Sub-Func@L24641`, `Sub-Closure@L24659`, `Sub-Async@L24677`, `def.UnionMember@L24696`
- `Sub-Member-Union@L24710`, `Sub-Union-Width@L24728`, `def.VarianceDomain@L24746`, `def.VarianceOfSignature@L24760`, `def.VarianceOf@L24774`, `def.VarianceSatisfied@L24788`, `Sub-Generic@L24806`, `Sub-Generic-Invariant-Err@L24824`
- `Sub-Generic-Covariant-Err@L24842`, `Sub-Generic-Contravariant-Err@L24860`, `Sub-Refl@L24878`, `Sub-Trans@L24896`, `def.TypeInferenceJudgementSet@L24917`, `def.TypeEqualityConstraint@L24931`, `def.TypeEqualityConstraintSet@L24945`, `req.ConstraintGenerationFeatureLocal@L24959`
- `def.TypeVariableDomain@L24973`, `def.TypeVariablesOfType@L24987`, `def.SubstitutionDomain@L25001`, `def.SubstitutionDefinedDomain@L25015`, `def.IdentitySubstitution@L25029`, `def.SubstitutionApplication@L25043`, `def.SubstitutionComposition@L25067`, `def.UnificationStateDomain@L25081`
- `Unify-Empty@L25095`, `Unify-Eq@L25112`, `Unify-Var-L@L25130`, `Unify-Var-R@L25148`, `Unify-Occurs-Fail@L25166`, `Unify-Tuple@L25184`, `Unify-Tuple-Fail@L25202`, `Unify-Array@L25220`
- `Unify-Array-Len-Fail@L25238`, `Unify-Slice@L25256`, `Unify-Perm@L25274`, `Unify-Perm-Fail@L25292`, `Unify-Func@L25310`, `Unify-Func-Fail@L25329`, `Unify-Closure@L25347`, `Unify-Closure-Fail@L25365`
- `Unify-Ptr@L25383`, `Unify-Ptr-State-Fail@L25401`, `Unify-RawPtr@L25419`, `Unify-RawPtr-Qual-Fail@L25437`, `Unify-Apply@L25455`, `Unify-Apply-Fail@L25473`, `Unify-Range@L25491`, `Unify-RangeInclusive@L25509`
- `Unify-RangeFrom@L25527`, `Unify-RangeTo@L25545`, `Unify-RangeToInclusive@L25563`, `Unify-Refine@L25581`, `Unify-Refine-Pred-Fail@L25599`, `Unify-Prim-Fail@L25617`, `Unify-Rigid-Fail@L25635`, `Unify-Ctor-Mismatch@L25660`
- `Unify-Ok@L25678`, `Unify-Err@L25696`, `Solve-Unify@L25714`, `Solve-Fail@L25732`, `Syn-Expr@L25750`, `Syn-Ident@L25768`, `Syn-Unit@L25786`, `Syn-Tuple@L25803`
- `Syn-Call@L25821`, `Syn-Call-Err@L25839`, `Chk-Subsumption-Modal-NonNiche@L25857`, `Chk-Subsumption@L25875`, `Chk-Null-Ptr@L25893`, `def.PtrNullExpectedType@L25911`, `Syn-PtrNull-Err@L25925`, `Chk-PtrNull-Err@L25943`
- `req.FeatureLocalSynthesisAndCheckingOwnership@L25961`, `property.TypeSystemMetatheory.Intro@L25978`, `Progress@L25992`, `Preservation@L26011`, `No-Use-After-Free@L26027`, `No-Double-Free@L26043`, `No-Dangling-Pointers@L26059`, `Exclusivity-Invariant@L26075`
- `Permission-Preservation@L26091`, `State-Determinism@L26107`, `No-Resurrection@L26123`, `Data-Race-Freedom@L26139`, `Fork-Join-Guarantee@L26155`, `Key-Serialization@L26171`, `Async-Key-Safety@L26187`, `req.PermissionQualifiedSubtypingPermissionEquality@L29069`
- `Parse-Record@L37876`, `Parse-RecordBody@L37894`, `Parse-RecordMemberList-End@L37912`, `Parse-RecordMemberList-Cons@L37930`, `Parse-RecordMember-Method@L37948`, `Parse-RecordMember-AssociatedType@L37966`, `Parse-RecordMember-Field@L37984`, `Parse-RecordFieldDeclAfterVis@L38002`
- `Parse-RecordFieldInitOpt-None@L38020`, `Parse-RecordFieldInitOpt-Yes@L38038`, `Parse-Record-Literal@L38056`, `def.RecordDeclAst@L38074`, `def.RecordMemberAst@L38091`, `def.RecordExprAst@L38109`, `def.RecordMembersSelectors@L38123`, `def.RecordPath@L38138`
- `Bind-Record@L38156`, `Resolve-RecordPath@L38173`, `ResolveQual-Name-Record@L38191`, `ResolveQual-Apply-RecordLit@L38209`, `ResolveItem-Record@L38227`, `def.RecordFieldInitOk@L38245`, `def.RecordFieldVisibility@L38259`, `WF-Record@L38274`
- `WF-Record-DupField@L38292`, `WF-RecordDecl@L38310`, `FieldVisOk-Err@L38328`, `def.RecordDefaultConstructible@L38346`, `def.RecordCallee@L38360`, `T-Record-Default@L38374`, `def.RecordFieldNameSets@L38392`, `def.RecordFieldLookup@L38410`
- `T-Record-Literal@L38426`, `EvalSigma-Record@L38460`, `EvalSigma-Record-Ctrl@L38478`, `ApplyRecordCtorSigma@L38510`, `ApplyRecordCtorSigma-Ctrl@L38528`, `Layout-Record-Empty@L38569`, `Layout-Record-Cons@L38586`, `Size-Record@L38604`
- `Align-Record@L38622`, `Layout-Record@L38640`, `LowerFieldInits-Empty@L38728`, `LowerFieldInits-Cons@L38745`, `Lower-Expr-Record@L38763`, `Lower-CallIR-RecordCtor@L38781`, `diagnostics.Records@L38799`, `Parse-Enum@L38866`
- `Parse-EnumBody@L38884`, `Parse-VariantMembers-Empty@L38902`, `Parse-VariantMembers-Cons@L38920`, `Parse-VariantSep-End@L38938`, `Parse-VariantSep-Terminator@L38956`, `Parse-Variant@L38974`, `Parse-VariantPayloadOpt-None@L38992`, `Parse-VariantPayloadOpt-Tuple@L39010`
- `Parse-VariantPayloadOpt-Record@L39028`, `Parse-VariantDiscriminantOpt-None@L39046`, `Parse-VariantDiscriminantOpt-Yes@L39064`, `req.EnumLiteralResolutionOwnership@L39096`, `def.EnumDeclAst@L39110`, `def.VariantDeclAst@L39127`, `def.EnumVariantHelpers@L39143`, `Bind-Enum@L39165`
- `def.EnumPayloadWellFormedness@L39182`, `Resolve-EnumUnit@L39200`, `Resolve-EnumTuple@L39218`, `Resolve-EnumRecord@L39236`, `ResolveQual-Name-Enum@L39254`, `ResolveQual-Apply-Enum-Tuple@L39272`, `ResolveQual-Apply-Enum-Record@L39290`, `ResolveItem-Enum@L39308`
- `def.EnumDiscriminantSequence@L39326`, `Enum-Disc-NotInt@L39348`, `Enum-Disc-Invalid@L39366`, `Enum-Disc-Negative@L39384`, `Enum-Disc-Dup@L39402`, `Enum-Empty-Err@L39420`, `Enum-Variant-Dup@L39438`, `def.EnumDiscriminantType@L39456`
- `WF-EnumDecl@L39475`, `def.EnumLiteralPayloadHelpers@L39493`, `T-Enum-Lit-Unit@L39511`, `Enum-Lit-Unknown@L39529`, `T-Enum-Lit-Tuple@L39547`, `Enum-Lit-Tuple-Arity-Err@L39565`, `T-Enum-Lit-Record@L39583`, `Enum-Lit-Record-MissingField@L39601`
- `EvalSigma-Enum-Unit@L39635`, `EvalSigma-Enum-Tuple@L39652`, `EvalSigma-Enum-Tuple-Ctrl@L39670`, `EvalSigma-Enum-Record@L39688`, `EvalSigma-Enum-Record-Ctrl@L39706`, `Layout-Enum-Tagged@L39753`, `Size-Enum@L39771`, `Align-Enum@L39789`
- `Layout-Enum@L39807`, `Lower-Expr-Enum-Unit@L39855`, `Lower-Expr-Enum-Tuple@L39872`, `Lower-Expr-Enum-Record@L39890`, `diagnostics.Enums@L39908`, `req.UnionIntroductionSemantic@L39955`, `Parse-UnionTail-None@L39971`, `Parse-UnionTail-Cons@L39989`
- `def.TypeUnionAst@L40007`, `def.UnionMemberSets@L40023`, `WF-Union@L40041`, `WF-Union-TooFew@L40059`, `def.UnionMember@L40077`, `Sub-Member-Union@L40091`, `Sub-Union-Width@L40109`, `T-Union-Intro@L40127`
- `Union-DirectAccess-Err@L40145`, `req.UnionMatchingPropagationOwnership@L40163`, `Layout-Union-Niche@L40350`, `Layout-Union-Tagged@L40368`, `Size-Union@L40386`, `Align-Union@L40404`, `Layout-Union@L40422`, `req.UnionDiagnosticOwnership@L40540`
- `Parse-Type-Alias@L40579`, `def.TypeAliasDeclAst@L40597`, `def.TypeAliasAccessors@L40613`, `Bind-TypeAlias@L40631`, `ResolveItem-TypeAlias@L40648`, `def.AliasNormalization@L40666`, `def.AliasPathNormalization@L40702`, `def.AliasTransparent@L40719`
- `def.AliasGraph@L40733`, `def.TypePaths@L40747`, `def.TypePathsOfModalRef@L40783`, `def.AliasCycle@L40798`, `TypeAlias-Ok@L40812`, `TypeAlias-Recursive-Err@L40829`, `req.TypeAliasDynamicSemantics@L40846`, `Size-Alias@L40864`
- `Align-Alias@L40882`, `Layout-Alias@L40900`, `req.TypeAliasDiagnosticOwnership@L40932`

#### `checker.attributes`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L26829-L27272.

- `req.MalformedAttributeSyntaxIllFormed@L26829`, `def.AttributeTargetDomain@L26843`, `def.AttributeRegistry@L26857`, `def.VendorAttributeRegistryInitial@L26871`, `def.SpecAttributeRegistry@L26885`, `def.SpecAttributeTargets@L26907`, `def.AttributeListWellFormedJudgementSet@L26942`, `AttrList-Ok@L26956`
- `AttrList-Unknown@L26974`, `AttrList-Target-Err@L26992`, `def.AttributeStaticSemantics.Helpers2@L27010`, `req.MemoryOrderAttributeTargets@L27024`, `req.AttributeListWellFormednessCheck@L27038`, `req.MultipleAttributeListConcatenation@L27052`, `req.FfiAttributeOwnership@L27066`, `conformance.VendorAttributeStaticSemantics@L27272`

#### `checker.attributes.layout`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L27401-L27559.

- `req.LayoutCRecordSemantics@L27401`, `req.LayoutCEnumSemantics@L27418`, `req.LayoutExplicitEnumDiscriminant@L27435`, `req.LayoutPackedRecordSemantics@L27454`, `req.PackedFieldReferenceRequiresUnsafe@L27471`, `req.LayoutAlignSemantics@L27487`, `def.ValidLayoutAttributeCombinations@L27506`, `def.InvalidLayoutAttributeCombinations@L27526`
- `def.LayoutAttributeApplicability@L27541`, `req.LayoutAttributeConstraints@L27559`

#### `checker.attributes.optimization`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27688-L27708.

- `req.InlineAttributeSemantics@L27688`, `req.ColdAttributeSemantics@L27708`

#### `checker.attributes.metadata`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L27837-L28187.

- `def.DynamicDeclarationPredicate@L27837`, `def.DynamicExpressionPredicate@L27851`, `def.DynamicScopePredicate@L27865`, `def.InDynamicContext@L27879`, `req.DeprecatedAttributeSemantics@L27895`, `req.DynamicAttributeSemantics@L27909`, `req.DynamicScopeDetermination@L27923`, `def.ComputeDynamicContext@L27939`
- `def.FindInnermostDynamic@L27962`, `def.MinimalSpan@L27983`, `DynamicContext-Override@L28001`, `req.DynamicContextOverridePropagation@L28021`, `DynamicContext-NoInherit-Call@L28035`, `req.DynamicContextLexicalNoCallPropagation@L28055`, `req.DynamicEffectsAndRestrictions@L28069`, `req.DynamicTargetRestrictions@L28086`
- `req.EmptyDynamicScopeWarning@L28103`, `req.StaleOkAttributeSemantics@L28117`, `req.VerificationModeAttributeSemantics@L28131`, `req.ReflectAttributeSemantics@L28145`, `req.DeriveAttributeSemantics@L28159`, `req.EmitAttributeSemantics@L28173`, `req.FilesAttributeSemantics@L28187`

#### `checker.permissions`

Count: 32 total; 32 required; 0 recommended; 0 informative. Ledger line span: L28361-L29129.

- `def.PermissionQualifierSemantics@L28361`, `req.PermissionRegimesDistinct@L28375`, `def.PermissionRegimeProperties@L28389`, `req.PermissionRegimeConstraints@L28409`, `def.SharedPermissionOperationMatrix@L28427`, `Layout-Perm@L28485`, `SizeOf-Perm@L28501`, `AlignOf-Perm@L28517`
- `conformance.AliasAndExclusivityRules@L28566`, `def.AliasingByOverlappingStorage@L28618`, `req.UniqueExclusivityInvariant@L28634`, `def.PermissionCoexistenceMatrix@L28650`, `req.BindingActivityNoConcreteSyntax@L28718`, `def.UniqueBindingActivityStates@L28768`, `Inactive-Enter@L28787`, `Inactive-Exit@L28805`
- `req.InactiveUniqueBindingNoDirectUse@L28823`, `req.BindingActivityNoAliasCreation@L28839`, `req.BindingActivityDeterministicReactivation@L28853`, `conformance.BindingActivityLowering@L28867`, `req.BindingActivityDiagnosticOwnership@L28883`, `req.PermissionAdmissibilityNoAdditionalSyntax@L28902`, `def.PermissionAdmissibilityAstInputs@L28934`, `req.PermissionAdmissibilityScope@L28953`
- `def.PermAdmitsJudgementSet@L28969`, `def.PermissionAdmissibilityPairs@L28983`, `req.PermAdmitsUseSitesNoTypeRewrite@L29003`, `def.MethodReceiverPermissionAdmissibility@L29017`, `def.MethodReceiverPermissionMatrix@L29035`, `req.PermissionAdmissibilityNoImplicitConversion@L29053`, `conformance.PermissionAdmissibilitySharedKeyGate@L29099`, `diagnostics.PermissionAdmissibility@L29129`

#### `checker.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L31062-L31062.

- `req.ExternBlockDiagnosticOwnership@L31062`

#### `checker.types.primitive`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L34345-L34624.

- `Parse-Prim-Type@L34345`, `Parse-Unit-Type@L34363`, `Parse-Never-Type@L34381`, `def.PrimitiveTypeName@L34399`, `def.TypePrimAst@L34415`, `def.NumericPrimitiveTypeSets@L34429`, `def.TypeWFJudgementSet@L34447`, `WF-Prim@L34463`
- `def.PrimitiveFloatRepresentation@L34481`, `def.DefaultNumericTypes@L34504`, `def.PrimitiveIntegerWidths@L34519`, `def.PrimitiveRangeOf@L34535`, `req.PrimitiveTypeDiagnosticOwnership@L34624`

#### `checker.types.tuples`

Count: 41 total; 41 required; 0 recommended; 0 informative. Ledger line span: L34683-L35482.

- `Parse-Tuple-Type@L34683`, `Parse-TupleTypeElems-Empty@L34701`, `Parse-TupleTypeElems-One@L34719`, `Parse-TupleTypeElems-Many@L34737`, `def.TupleScanDelimiterDeltas@L34755`, `TupleScan-EOF@L34813`, `TupleScan-EndParen@L34831`, `TupleScan-SingletonComma@L34848`
- `TupleScan-Separator@L34865`, `TupleScan-Advance@L34882`, `def.TupleParen@L34899`, `Parse-Tuple-Literal@L34913`, `Parse-TupleExprElems-Empty@L34931`, `Parse-TupleExprElems-Single@L34949`, `Parse-TupleExprElems-Many@L34967`, `Postfix-TupleIndex@L34985`
- `def.TupleTypeAst@L35003`, `def.TupleExpressionAst@L35019`, `WF-Tuple@L35050`, `T-Tuple-Unit@L35068`, `T-Tuple@L35085`, `T-Tuple-Index@L35103`, `T-Tuple-Index-Perm@L35121`, `P-Tuple-Index@L35139`
- `P-Tuple-Index-Perm@L35157`, `def.ConstTupleIndex@L35175`, `TupleIndex-NonConst@L35189`, `TupleIndex-OOB@L35207`, `TupleAccess-NotTuple@L35225`, `req.TuplePatternRulesOwnership@L35243`, `EvalSigma-Tuple@L35273`, `EvalSigma-Tuple-Ctrl@L35291`
- `EvalSigma-TupleAccess@L35309`, `EvalSigma-TupleAccess-Ctrl@L35327`, `Layout-Tuple-Empty@L35361`, `Layout-Tuple-Cons@L35378`, `Size-Tuple@L35396`, `Align-Tuple@L35414`, `Layout-Tuple@L35432`, `Lower-Expr-Tuple@L35464`
- `diagnostics.Tuples@L35482`

#### `checker.types.arrays`

Count: 34 total; 34 required; 0 recommended; 0 informative. Ledger line span: L35530-L36185.

- `Parse-Array-Type@L35530`, `Parse-Array-Segment-Elem@L35548`, `Parse-Array-Segment-Repeat@L35566`, `Parse-Array-Segment-List-Empty@L35584`, `Parse-Array-Segment-List-Single@L35601`, `Parse-Array-Segment-List-Comma@L35619`, `Parse-Array-Literal@L35637`, `Postfix-Index@L35655`
- `def.ArrayAstForms@L35673`, `req.IndexAccessArrayOwnership@L35691`, `def.ConstIndex@L35705`, `WF-Array@L35721`, `def.ArraySegmentLength@L35739`, `T-Array-Literal-Segments@L35754`, `T-Index-Array@L35780`, `T-Index-Array-Dynamic@L35798`
- `T-Index-Array-Perm@L35816`, `T-Index-Array-Perm-Dynamic@L35834`, `P-Index-Array@L35852`, `P-Index-Array-Perm@L35870`, `P-Index-Array-Dynamic@L35888`, `P-Index-Array-Perm-Dynamic@L35906`, `Index-Array-NonConst-Err@L35924`, `Index-Array-OOB-Err@L35942`
- `Index-Array-NonUsize@L35960`, `EvalSigma-Array@L36011`, `EvalSigma-Array-Ctrl@L36029`, `EvalSigma-Index@L36047`, `EvalSigma-Index-OOB@L36065`, `Size-Array@L36085`, `Align-Array@L36103`, `Layout-Array@L36121`
- `Lower-Expr-Array@L36167`, `req.ArrayDiagnosticOwnership@L36185`

#### `checker.types.slices`

Count: 28 total; 28 required; 0 recommended; 0 informative. Ledger line span: L36224-L36756.

- `req.ArrayToSliceCoercionSemantic@L36224`, `Parse-Slice-Type@L36240`, `req.IndexAccessSliceOwnership@L36258`, `def.TypeSliceAst@L36272`, `req.IndexAccessSliceExpressionSemantics@L36288`, `def.RangeIndexType@L36302`, `WF-Slice@L36318`, `T-Index-Slice@L36336`
- `T-Index-Slice-Perm@L36354`, `T-Slice-From-Array@L36372`, `T-Slice-From-Array-Perm@L36390`, `T-Slice-From-Slice@L36408`, `T-Slice-From-Slice-Perm@L36426`, `P-Index-Slice@L36444`, `P-Index-Slice-Perm@L36462`, `P-Slice-From-Array@L36480`
- `P-Slice-From-Array-Perm@L36498`, `P-Slice-From-Slice@L36516`, `P-Slice-From-Slice-Perm@L36534`, `Coerce-Array-Slice@L36552`, `Index-NonIndexable@L36570`, `EvalSigma-Index-Range@L36619`, `EvalSigma-Index-Range-OOB@L36637`, `Size-Slice@L36657`
- `Align-Slice@L36674`, `Layout-Slice@L36691`, `Index-Slice-NonUsize@L36738`, `req.SliceDiagnosticOwnership@L36756`

#### `checker.types.ranges`

Count: 55 total; 55 required; 0 recommended; 0 informative. Ledger line span: L36813-L37833.

- `Parse-Range-To@L36813`, `Parse-Range-ToInc@L36831`, `Parse-Range-Full@L36849`, `Parse-Range-Lhs@L36867`, `Parse-RangeTail-None@L36885`, `Parse-RangeTail-From@L36903`, `Parse-RangeTail-Exclusive@L36921`, `Parse-RangeTail-Inclusive@L36939`
- `def.RangeSurfaceTypeElaboration@L36957`, `def.RangeTypeAst@L36978`, `def.RangeExprAst@L36994`, `def.IsRangeType@L37008`, `def.RangeFullExprType@L37022`, `def.RangeToExprType@L37038`, `def.RangeToInclusiveExprType@L37052`, `def.RangeFromExprType@L37066`
- `def.RangeExclusiveExprType@L37080`, `def.RangeInclusiveExprType@L37094`, `T-Range-Lift@L37108`, `Range-Full@L37126`, `Range-To@L37143`, `Range-ToInclusive@L37161`, `Range-From@L37179`, `Range-Exclusive@L37197`
- `Range-Inclusive@L37215`, `req.RangePatternSemanticsOwnership@L37233`, `EvalSigma-Range@L37268`, `EvalSigma-Range-Ctrl@L37286`, `EvalSigma-Range-Ctrl-Hi@L37304`, `def.RangeInc@L37322`, `Lower-Range-Full@L37405`, `Lower-Range-To@L37422`
- `Lower-Range-ToInclusive@L37440`, `Lower-Range-From@L37458`, `Lower-Range-Inclusive@L37476`, `Lower-Range-Exclusive@L37494`, `Size-Range@L37512`, `Align-Range@L37530`, `Layout-Range@L37548`, `Size-RangeInclusive@L37566`
- `Align-RangeInclusive@L37584`, `Layout-RangeInclusive@L37602`, `Size-RangeFrom@L37620`, `Align-RangeFrom@L37638`, `Layout-RangeFrom@L37656`, `Size-RangeTo@L37674`, `Align-RangeTo@L37692`, `Layout-RangeTo@L37710`
- `Size-RangeToInclusive@L37728`, `Align-RangeToInclusive@L37746`, `Layout-RangeToInclusive@L37764`, `Size-RangeFull@L37782`, `Align-RangeFull@L37799`, `Layout-RangeFull@L37816`, `req.RangeDiagnosticOwnership@L37833`

#### `spec.modal-special`

Count: 386 total; 386 required; 0 recommended; 0 informative. Ledger line span: L40979-L46752.

- `grammar.ModalDeclarations.Syntax@L40979`, `rule.13.Parse-Modal@L41001`, `rule.13.Parse-ModalBody@L41017`, `rule.13.Parse-StateBlock@L41033`, `def.ModalTypeRef.Parser@L41051`, `rule.13.Parse-Modal-State-Type@L41063`, `rule.13.Parse-Record-Literal-ModalState@L41079`, `req.ModalStateMemberDispatch@L41095`
- `def.ModalDeclAst@L41109`, `def.StateBlockAst@L41122`, `def.StateMemberAst@L41134`, `def.ModalRefAst@L41150`, `def.TypeRefModalStateAst@L41162`, `def.ModalStateAccessors@L41174`, `def.ModalStatePayload@L41191`, `def.BuiltinModalSet@L41203`
- `def.ModalPath@L41215`, `def.ModalSelfRef@L41228`, `def.ModalSelfTypes@L41243`, `def.ModalRefAccessors@L41256`, `def.ModalDeclOf@L41273`, `def.ModalRefSubst@L41285`, `def.ModalPayloadSubstitution@L41297`, `def.PayloadMap@L41309`
- `def.ModalPayloadMap@L41323`, `rule.13.WF-Modal-Payload@L41339`, `rule.13.Modal-Payload-DupField@L41355`, `rule.13.WF-ModalState@L41371`, `rule.13.WF-ModalState-ArgCount-Err@L41387`, `def.StateMemberVisOk@L41403`, `rule.13.WF-ModalDecl@L41415`, `rule.13.StateMemberVisOk-Err@L41431`
- `rule.13.Modal-WF@L41447`, `rule.13.Modal-NoStates-Err@L41463`, `rule.13.Modal-DupState-Err@L41479`, `rule.13.Modal-StateName-Err@L41495`, `rule.13.State-Specific-WF@L41511`, `def.ModalPayloadNames@L41527`, `rule.13.T-Modal-State-Intro@L41540`, `rule.13.Record-FileDir-Err@L41556`
- `def.RegionPayload@L41574`, `def.RegionProcSet@L41589`, `def.RegionNewScopedProcSig@L41601`, `def.RegionAllocProcSig@L41613`, `def.RegionResetUncheckedProcSig@L41625`, `def.RegionFreezeProcSig@L41637`, `def.RegionThawProcSig@L41649`, `def.RegionFreeUncheckedProcSig@L41661`
- `def.RegionProvenanceTypeHelpers@L41673`, `def.RegionNonBitcopy@L41687`, `req.RegionAllocProvenance@L41701`, `req.RegionInactiveDereferenceSemantics@L41713`, `req.RegionFreeAtScopeExit@L41725`, `rule.13.Region-Unchecked-Unsafe-Err@L41737`, `def.CancelTokenTypePresence@L41753`, `def.CancelTokenPayload@L41766`
- `def.CancelTokenMembersAndDecl@L41779`, `def.CancelTokenTypeBinding@L41801`, `def.CancelTokenProcSignatures@L41813`, `def.SpawnedTypePresence@L41826`, `def.SpawnedPayload@L41839`, `def.SpawnedMembersAndDecl@L41855`, `def.SpawnedTypeBinding@L41875`, `def.TrackedTypePresence@L41887`
- `def.TrackedPayload@L41900`, `def.TrackedMembersAndDecl@L41916`, `def.TrackedTypeBinding@L41936`, `def.DirIterMembersAndDecl@L41948`, `def.FileMembersAndDecl@L41970`, `def.DirIterAndFileTypeBindings@L42007`, `req.AsyncModalDefinedInChapter21@L42020`, `def.ModalValRuntime@L42034`
- `def.RecordModalStateValueType@L42046`, `def.ModalValValueType@L42058`, `req.ModalRuntimeRepresentation@L42070`, `def.ModalDiscType@L42084`, `def.ModalStateLayoutMetrics@L42096`, `def.ModalSingleFieldPayload@L42109`, `def.ModalEmptyState@L42122`, `def.ModalPayloadState@L42134`
- `def.ModalNicheApplies@L42146`, `def.ModalStateValueBits@L42158`, `def.ModalEmptyStates@L42170`, `def.EmptyRecordVal@L42182`, `def.ModalNicheBits@L42194`, `def.ModalBits@L42206`, `def.ModalAlign@L42218`, `def.ModalSize@L42230`
- `def.ModalPayloadSize@L42242`, `def.ModalPayloadAlign@L42254`, `def.StateRecordBits@L42266`, `def.ModalPayloadBits@L42278`, `def.ModalLayoutJudgementSet@L42290`, `rule.13.Layout-Modal-Niche@L42302`, `rule.13.Layout-Modal-Tagged@L42318`, `rule.13.Size-Modal@L42334`
- `rule.13.Align-Modal@L42350`, `rule.13.Layout-Modal@L42366`, `rule.13.Size-ModalState@L42382`, `rule.13.Align-ModalState@L42398`, `rule.13.Layout-ModalState@L42414`, `def.ModalStateLayoutEquation@L42430`, `def.EmptyModalStateSizeEquation@L42442`, `def.ModalBaseLayoutEquation@L42454`
- `req.ModalTaggedPaddingZero@L42468`, `def.ModalTaggedBits@L42479`, `def.ModalRefValueBits@L42491`, `diag.ModalDeclarations@L42505`, `grammar.StateFields.Syntax@L42521`, `rule.13.Parse-StateMember-Field@L42537`, `def.StateFieldDeclAst@L42555`, `def.PayloadNameHelpers@L42567`
- `def.ModalFieldVisible@L42582`, `rule.13.T-Modal-Field@L42594`, `rule.13.T-Modal-Field-Perm@L42610`, `rule.13.Modal-Field-Missing@L42626`, `rule.13.Modal-Field-General-Err@L42642`, `rule.13.Modal-Field-NotVisible@L42658`, `req.StateFieldDynamic@L42676`, `req.StateFieldLowering@L42690`
- `diag.StateFields@L42704`, `grammar.StateMethods.Syntax@L42720`, `rule.13.Parse-StateMember-Method@L42737`, `req.StateMethodSignatureParser@L42753`, `def.StateMethodDeclAst@L42767`, `def.StateMethodCollections@L42779`, `def.StateMethodSig@L42792`, `def.LookupStateMethod@L42806`
- `def.StateMemberVisible@L42821`, `rule.13.StateMethod-Dup@L42833`, `rule.13.WF-State-Method@L42849`, `rule.13.T-Modal-Method@L42865`, `rule.13.Modal-Method-RecvPerm-Err@L42881`, `rule.13.Modal-Method-NotFound@L42897`, `rule.13.Modal-Method-NotVisible@L42913`, `rule.13.T-Modal-Method-Body@L42929`
- `def.StateMethodTarget@L42947`, `rule.13.ApplyMethodSigma@L42959`, `req.BuiltinStateMethodCalling@L42975`, `req.StateMethodLowering@L42989`, `diag.StateMethods@L43003`, `grammar.Transitions.Syntax@L43019`, `rule.13.Parse-StateMember-Transition@L43035`, `def.TransitionDeclAst@L43053`
- `def.TransitionCollections@L43065`, `def.LookupTransition@L43079`, `def.TransitionSig@L43092`, `rule.13.Transition-Dup@L43111`, `rule.13.StateMember-Name-Conflict@L43127`, `rule.13.WF-Transition@L43143`, `rule.13.Transition-Target-Err@L43159`, `rule.13.T-Modal-Transition@L43175`
- `rule.13.Transition-Source-Err@L43191`, `rule.13.Transition-NotVisible@L43207`, `rule.13.T-Modal-Transition-Body@L43223`, `rule.13.Transition-Body-Err@L43239`, `def.TransitionMethodTarget@L43257`, `req.TransitionRuntimeSemantics@L43269`, `def.IsTransition@L43281`, `def.TransitionTarget@L43293`
- `rule.13.ApplyTransitionSigma@L43305`, `def.ExtractReturnValue@L43323`, `def.ValidateModalState@L43336`, `req.TransitionLowering@L43350`, `diag.Transitions@L43364`, `grammar.ModalWidening.Syntax@L43380`, `rule.13.Parse-Unary-Widen@L43396`, `def.ModalWidening.AST@L43414`
- `def.ModalWideningThreshold@L43428`, `rule.13.T-Modal-Widen@L43440`, `rule.13.T-Modal-Widen-Perm@L43456`, `rule.13.Widen-AlreadyGeneral@L43472`, `rule.13.Widen-NonModal@L43488`, `def.NicheCompatible@L43504`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43516`, `def.WidenWarnCond@L43532`
- `rule.13.Warn-Widen-LargePayload@L43544`, `rule.13.Warn-Widen-Ok@L43560`, `def.ModalWideningDynamic@L43578`, `req.ModalWideningLowering@L43592`, `def.ModalStateSizeBound@L43604`, `diag.ModalWidening@L43618`, `grammar.StringTypes.Syntax@L43634`, `rule.13.Parse-String-Type@L43651`
- `rule.13.Parse-StringState-None@L43669`, `rule.13.Parse-StringState-Managed@L43685`, `rule.13.Parse-StringState-View@L43701`, `def.TypeStringAst@L43719`, `def.StringStateSet@L43731`, `def.StringBuiltinTable@L43743`, `def.StringBuiltinSig@L43764`, `rule.13.WF-String@L43778`
- `rule.13.Sub-String-State@L43794`, `req.StringBuiltinsTyping@L43808`, `def.StringLiteralVal@L43822`, `def.StringBytesStoreDomains@L43834`, `def.ViewBytes@L43850`, `def.ByteSeqOf@L43862`, `def.ByteLen@L43877`, `def.StringValueTypes@L43889`
- `def.StringBytesJudgementSet@L43903`, `req.StringLiteralStorage@L43923`, `rule.13.StringFrom-Ok@L43937`, `rule.13.StringFrom-Err@L43953`, `rule.13.StringAsView-Ok@L43969`, `rule.13.StringToManaged-Ok@L43985`, `rule.13.StringToManaged-Err@L44001`, `rule.13.StringCloneWith-Ok@L44017`
- `rule.13.StringCloneWith-Err@L44033`, `rule.13.StringAppend-Ok@L44049`, `rule.13.StringAppend-Err@L44065`, `rule.13.StringLength@L44081`, `rule.13.StringIsEmpty@L44097`, `def.StringViewOf@L44113`, `def.StringRuntimeLength@L44127`, `def.StringManagedLoweringLayout@L44142`
- `def.StringViewLoweringLayout@L44156`, `rule.13.Size-String-Managed@L44170`, `rule.13.Align-String-Managed@L44186`, `rule.13.Layout-String-Managed@L44202`, `rule.13.Size-String-View@L44218`, `rule.13.Align-String-View@L44234`, `rule.13.Layout-String-View@L44250`, `rule.13.Size-String-Modal@L44266`
- `rule.13.Align-String-Modal@L44282`, `def.StringValueBits@L44298`, `def.DropManagedString@L44310`, `diag.StringTypes@L44324`, `grammar.BytesTypes.Syntax@L44340`, `rule.13.Parse-Bytes-Type@L44357`, `rule.13.Parse-BytesState-None@L44375`, `rule.13.Parse-BytesState-Managed@L44391`
- `rule.13.Parse-BytesState-View@L44407`, `def.TypeBytesAst@L44425`, `def.BytesStateSet@L44437`, `def.BytesBuiltinTable@L44449`, `def.StringBytesBuiltinTable@L44473`, `def.BytesBuiltinSig@L44485`, `def.StringBytesBuiltinSig@L44497`, `rule.13.WF-Bytes@L44512`
- `rule.13.Sub-Bytes-State@L44528`, `req.BytesBuiltinsTyping@L44542`, `def.SliceBytes@L44556`, `def.BytesValueTypes@L44568`, `def.BytesJudgementSet@L44582`, `def.StringBytesJudgementSet@L44605`, `rule.13.BytesWithCapacity-Ok@L44617`, `rule.13.BytesWithCapacity-Err@L44633`
- `rule.13.BytesFromSlice-Ok@L44649`, `rule.13.BytesFromSlice-Err@L44665`, `rule.13.BytesAsView-Ok@L44681`, `rule.13.BytesToManaged-Ok@L44697`, `rule.13.BytesToManaged-Err@L44713`, `rule.13.BytesView-Ok@L44729`, `rule.13.BytesViewString-Ok@L44745`, `rule.13.BytesAsSlice-Ok@L44761`
- `rule.13.BytesAppend-Ok@L44777`, `rule.13.BytesAppend-Err@L44793`, `rule.13.BytesLength@L44809`, `rule.13.BytesIsEmpty@L44825`, `def.BytesViewOf@L44841`, `def.BytesRuntimeLength@L44855`, `def.BytesViewConversions@L44868`, `def.BytesManagedLoweringLayout@L44883`
- `def.BytesViewLoweringLayout@L44897`, `rule.13.Size-Bytes-Managed@L44911`, `rule.13.Align-Bytes-Managed@L44927`, `rule.13.Layout-Bytes-Managed@L44943`, `rule.13.Size-Bytes-View@L44959`, `rule.13.Align-Bytes-View@L44975`, `rule.13.Layout-Bytes-View@L44991`, `rule.13.Size-Bytes-Modal@L45007`
- `rule.13.Align-Bytes-Modal@L45023`, `def.BytesValueBits@L45039`, `def.DropManagedBytes@L45051`, `diag.BytesTypes@L45065`, `grammar.SafePointerTypes.Syntax@L45081`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45098`, `rule.13.Parse-Safe-Pointer-Type@L45114`, `rule.13.Parse-PtrState-None@L45132`
- `rule.13.Parse-PtrState-Valid@L45148`, `rule.13.Parse-PtrState-Null@L45164`, `rule.13.Parse-PtrState-Expired@L45180`, `def.PtrStateSet@L45198`, `def.SafePointerTypeForms@L45210`, `rule.13.WF-Ptr@L45225`, `def.SafePointerTraits@L45241`, `rule.13.Sub-Ptr-State@L45255`
- `def.SafePointerRuntimeConstructors@L45273`, `def.SafePointerValueType@L45287`, `def.PtrStateImmediate@L45299`, `def.PtrStateValid@L45312`, `def.PtrAddrJudgementSet@L45327`, `rule.13.ReadPtr-Safe@L45339`, `rule.13.WritePtr-Safe@L45355`, `rule.13.ReadPtr-Null@L45371`
- `rule.13.ReadPtr-Expired@L45387`, `rule.13.WritePtr-Null@L45403`, `rule.13.WritePtr-Expired@L45419`, `rule.13.Size-Ptr@L45437`, `rule.13.Align-Ptr@L45453`, `rule.13.Layout-Ptr@L45469`, `def.SafePointerSizeAlignEquations@L45485`, `def.PtrDiagRefs@L45497`
- `def.SafePointerNicheSet@L45509`, `def.SafePointerValidValue@L45522`, `def.SafePointerValueBits@L45537`, `diag.SafePointerTypes@L45554`, `grammar.RawPointerTypes.Syntax@L45570`, `rule.13.Parse-Raw-Pointer-Type@L45586`, `def.RawPointerTypes.AST@L45604`, `rule.13.WF-RawPtr@L45618`
- `rule.13.T-Deref-Raw@L45634`, `rule.13.P-Deref-Raw-Imm@L45650`, `rule.13.P-Deref-Raw-Mut@L45666`, `rule.13.Deref-Raw-Unsafe@L45682`, `def.RawPointerRuntimeValue@L45700`, `rule.13.ReadPtr-Raw@L45712`, `rule.13.WritePtr-Raw@L45728`, `rule.13.ReadPtr-Raw-Invalid@L45744`
- `rule.13.WritePtr-Raw-Imm@L45760`, `rule.13.WritePtr-Raw-Invalid@L45776`, `rule.13.Size-RawPtr@L45794`, `rule.13.Align-RawPtr@L45810`, `rule.13.Layout-RawPtr@L45826`, `def.RawPointerValidValue@L45842`, `def.RawPointerValueBits@L45854`, `req.RawPointerLowering@L45865`
- `diag.RawPointerTypes@L45879`, `grammar.FunctionTypes.Syntax@L45895`, `req.FunctionTypeTrailingComma@L45911`, `rule.13.Parse-Func-Type@L45925`, `rule.13.Parse-ParamType-Move@L45941`, `rule.13.Parse-ParamType-Plain@L45957`, `rule.13.Parse-ParamTypeList-Empty@L45973`, `rule.13.Parse-ParamTypeList-Cons@L45989`
- `rule.13.Parse-ParamTypeListTail-End@L46005`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46021`, `rule.13.Parse-ParamTypeListTail-Cons@L46037`, `def.FunctionTypes.AST@L46055`, `rule.13.WF-Func@L46069`, `rule.13.T-Equiv-Func@L46085`, `rule.13.Sub-Func@L46101`, `rule.13.T-Proc-As-Value@L46117`
- `diag.FunctionTypeCalls@L46133`, `def.FunctionRuntimeValue@L46147`, `rule.13.EvalSigma-Call-Proc@L46159`, `req.NamedProceduresFirstClass@L46175`, `rule.13.Size-Func@L46189`, `rule.13.Align-Func@L46205`, `rule.13.Layout-Func@L46221`, `req.FunctionTypeCallLowering@L46237`
- `diag.FunctionTypes@L46251`, `grammar.ClosureTypes.Syntax@L46267`, `req.ClosureParamUnionParentheses@L46284`, `rule.13.Parse-Closure-Type@L46298`, `rule.13.Parse-Closure-Type-Empty@L46314`, `rule.13.Parse-ClosureParamType-Grouped@L46330`, `rule.13.Parse-ClosureParamType-Plain@L46346`, `rule.13.Parse-ClosureParamTypeList-Empty@L46362`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46378`, `rule.13.Parse-ClosureParamTypeListTail-End@L46394`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46410`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46426`, `rule.13.Parse-ClosureDepsOpt-None@L46442`, `rule.13.Parse-ClosureDepsOpt-Some@L46458`, `rule.13.Parse-SharedDepList-Empty@L46474`, `rule.13.Parse-SharedDepList-Single@L46490`
- `rule.13.Parse-SharedDepList-Cons@L46506`, `rule.13.Parse-SharedDep@L46522`, `def.TypeClosureAst@L46540`, `def.ClosureDepsOpt@L46552`, `req.ClosureTypeOwnershipBoundaries@L46564`, `rule.13.WF-Closure@L46578`, `rule.13.T-Equiv-Closure@L46594`, `rule.13.Sub-Closure@L46610`
- `req.ClosureExpressionOwnership@L46626`, `def.ClosureRuntimeValue@L46640`, `req.ClosureOperationOwnership@L46652`, `def.ClosureLoweringRep@L46666`, `rule.13.Size-Closure@L46678`, `rule.13.Align-Closure@L46694`, `rule.13.Layout-Closure@L46710`, `req.ClosureLoweringOwnership@L46726`
- `diag.ClosureTypes@L46740`, `diagnostics.ModalPointerSupplement@L46752`

#### `spec.abstraction-polymorphism`

Count: 328 total; 327 required; 0 recommended; 0 informative. Ledger line span: L46800-L51908.

- `grammar.GenericParamsAndArgsSyntax@L46800`, `req.GenericArgsTrailingComma@L46818`, `req.GenericParamInlineBoundsClassOnly@L46830`, `rule.14.Parse-GenericArgs@L46844`, `rule.14.Parse-GenericArgsOpt-None@L46860`, `rule.14.Parse-GenericArgsOpt-Yes@L46876`, `rule.14.Parse-GenericParamsOpt-None@L46892`, `rule.14.Parse-GenericParamsOpt-Yes@L46908`
- `rule.14.Parse-GenericParams@L46924`, `rule.14.Parse-TypeParamTail-End@L46940`, `rule.14.Parse-TypeParamTail-Cons@L46956`, `rule.14.Parse-TypeParam@L46972`, `rule.14.Parse-TypeBoundsOpt-None@L46988`, `rule.14.Parse-TypeBoundsOpt-Yes@L47004`, `rule.14.Parse-ClassBoundList-Cons@L47020`, `rule.14.Parse-ClassBoundListTail-End@L47036`
- `rule.14.Parse-ClassBoundListTail-Cons@L47052`, `rule.14.Parse-ClassBound@L47068`, `rule.14.Parse-TypeDefaultOpt-None@L47084`, `rule.14.Parse-TypeDefaultOpt-Yes@L47100`, `rule.14.Parse-PredicateClauseOpt-None@L47116`, `rule.14.Parse-PredicateClauseOpt-Yes@L47132`, `rule.14.Parse-PredicateReqList-Cons@L47148`, `rule.14.Parse-PredicateReqListTail-End@L47164`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47180`, `rule.14.Parse-PredicateReqListTail-Cons@L47196`, `def.PredicateNameParserSet@L47212`, `rule.14.Parse-PredicateReq-Predicate@L47224`, `rule.14.Parse-PredicateReq-Err@L47240`, `def.VarianceSet@L47258`, `def.GenericParamAst@L47270`, `def.PredicateClauseAst@L47284`
- `def.GenericParamHelpers@L47298`, `def.GenericDefaultWellFormedness@L47317`, `rule.14.WF-Generic-Param@L47331`, `def.DefaultArgs@L47347`, `rule.14.PredicateReq-WF-Predicate@L47364`, `def.PredicateClauseWellFormedness@L47380`, `def.PredOk@L47392`, `rule.14.T-Constraint-Sat@L47407`
- `rule.14.PredicateReq-Predicate@L47423`, `def.PredicateClauseSubstitutionOk@L47439`, `req.GenericBoundsAndPredicatesConjunctive@L47451`, `conformance.GenericParamsNoRuntimeSemantics@L47465`, `conformance.GenericParamsLoweringInputsOnly@L47479`, `diag.GenericParametersAndArguments@L47493`, `grammar.GenericProceduresAndTypesSyntax@L47509`, `req.GenericParamsNominalOwnerChapters@L47525`
- `req.GenericDeclarationParsingDelegated@L47539`, `def.CallTypeArgsStart@L47551`, `rule.14.Postfix-Call-TypeArgs@L47563`, `def.GenericDeclarationAstExtensions@L47581`, `def.GenericApplyAst@L47598`, `def.GenericDeclarationAccessors@L47612`, `rule.14.WF-Generic-Proc@L47627`, `def.GenericCalleeProc@L47643`
- `def.GenericInferenceFreshArgs@L47657`, `def.InferTypeArgs@L47670`, `rule.14.GenericCallInference@L47688`, `rule.14.T-Generic-Call@L47719`, `rule.14.Generic-Call-ArgCount-Err@L47742`, `rule.14.WF-Path-Generic-Err@L47758`, `rule.14.WF-Apply@L47774`, `rule.14.WF-Apply-ArgCount-Err@L47790`
- `req.GenericCallInferenceElaboration@L47806`, `conformance.GenericInstantiationDynamicElaboration@L47820`, `conformance.GenericMonomorphicInstantiationsDistinct@L47832`, `def.MonomorphizationSpecialization@L47846`, `req.GenericProcedureCallLowering@L47860`, `req.GenericInstantiationIndependentLowering@L47872`, `req.GenericInfiniteMonomorphizationRejected@L47884`, `req.GenericInstantiationDepthLimit@L47896`
- `req.GenericNominalSizeAlignSubstitutedBody@L47908`, `diag.GenericProceduresAndTypes@L47922`, `grammar.ClassesSyntax@L47938`, `req.AssociatedTypeSyntaxCanonicalOwner@L47955`, `rule.14.Parse-Class@L47969`, `rule.14.Parse-Superclass-None@L47985`, `rule.14.Parse-Superclass-Yes@L48001`, `rule.14.Parse-SuperclassBounds-Cons@L48017`
- `rule.14.Parse-SuperclassBoundsTail-End@L48033`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48049`, `rule.14.Parse-ClassBody@L48065`, `rule.14.Parse-ClassItemList-End@L48081`, `rule.14.Parse-ClassItemList-Cons@L48097`, `rule.14.Parse-ClassItem-Method@L48113`, `rule.14.Parse-ClassItem-Field@L48129`, `rule.14.Parse-ClassItem-AbstractState@L48145`
- `rule.14.Parse-ClassMethodBody-Concrete@L48161`, `rule.14.Parse-ClassMethodBody-Abstract@L48177`, `def.ClassDeclAst@L48195`, `def.ClassItemAst@L48208`, `def.ClassMethodAbstractConcretePredicates@L48225`, `def.ClassMemberCollections@L48238`, `def.ClassMethodReturnType@L48254`, `def.SelfVar@L48267`
- `def.DistinctDisjoint@L48281`, `rule.14.WF-ClassPath@L48294`, `rule.14.WF-ClassPath-Err@L48310`, `def.SubstSelf@L48326`, `def.ReceiverTypeHelpers@L48352`, `def.Supers@L48381`, `rule.14.T-Superclass@L48393`, `def.ClassLinearizationMergeHelpers@L48409`
- `rule.14.Lin-Base@L48429`, `rule.14.Merge-Empty@L48445`, `rule.14.Merge-Step@L48461`, `rule.14.Merge-Fail@L48477`, `rule.14.Lin-Ok@L48493`, `rule.14.Lin-Fail@L48509`, `rule.14.Superclass-Cycle@L48525`, `def.LinearizeHeadInvariant@L48541`
- `def.EffectiveClassMembers@L48553`, `def.FirstByName@L48566`, `rule.14.EffMethods-Conflict@L48583`, `def.FieldSig@L48599`, `def.FirstFieldByName@L48611`, `rule.14.EffFields-Conflict@L48628`, `def.SelfTypeClass@L48644`, `rule.14.WF-Class-Method@L48656`
- `rule.14.T-Class-Method-Body-Abstract@L48672`, `rule.14.T-Class-Method-Body@L48688`, `rule.14.WF-Class@L48704`, `conformance.ClassDeclarationsNoRuntimeActions@L48722`, `req.ClassMethodLowering@L48736`, `req.ClassDefaultMethodAndVtableOwnership@L48748`, `diag.Classes@L48762`, `grammar.ImplementationsSyntax@L48778`
- `req.NoStandaloneImplementationBlocks@L48793`, `rule.14.Parse-Implements-None@L48807`, `rule.14.Parse-Implements-Yes@L48823`, `rule.14.Parse-ClassList-Cons@L48839`, `rule.14.Parse-ClassListTail-End@L48855`, `rule.14.Parse-ClassListTail-Comma@L48871`, `def.ImplementsAccessor@L48889`, `req.SubtypeOperatorImplementationMeaning@L48901`
- `req.ImplementationsConcreteOwnerOnly@L48916`, `def.ImplementerFields@L48928`, `def.ImplementerMethods@L48941`, `def.MethodByName@L48954`, `def.ClassEffectiveTables@L48967`, `def.ImplementationOrphanRule@L48980`, `def.DefaultMethodPredicates@L48996`, `rule.14.Impl-Abstract-Method@L49009`
- `rule.14.Impl-Missing-Method@L49025`, `rule.14.Impl-AssocType-Missing@L49041`, `rule.14.Impl-Sig-Err@L49057`, `rule.14.Override-Abstract-Err@L49073`, `rule.14.Impl-Concrete-Default@L49089`, `rule.14.Impl-Concrete-Override@L49105`, `rule.14.Override-Missing-Err@L49121`, `rule.14.Impl-Sig-Err-Concrete@L49137`
- `rule.14.Override-NoConcrete@L49153`, `rule.14.Impl-Field@L49169`, `rule.14.Impl-Field-Missing@L49185`, `rule.14.Impl-Field-Type-Err@L49201`, `rule.14.Impl-Coherence-Err@L49217`, `rule.14.Impl-Orphan-Err@L49233`, `rule.14.WF-Impl@L49249`, `rule.14.ImplementationSubtypeRelation@L49265`
- `req.14.ModalClassImplementationRequiresModalType@L49278`, `req.14.DuplicateClassImplementationForbidden@L49291`, `req.14.ImplementationOrphanRequirement@L49304`, `req.14.ImplementationsNoAdditionalRuntimeState@L49319`, `req.14.ImplementationBodyLowering@L49334`, `diag.14.Implementations@L49349`, `grammar.14.AssociatedType@L49366`, `req.14.AssociatedTypeEqualsMeaning@L49381`
- `rule.14.Parse-ClassItem-AssociatedType@L49396`, `rule.14.Parse-AssocTypeOpt-None@L49412`, `rule.14.Parse-AssocTypeOpt-Yes@L49428`, `rule.14.Parse-AssocTypeDefaultOpt@L49444`, `rule.14.Parse-RecordMember-AssociatedType@L49460`, `def.14.AssociatedTypeDeclAst@L49478`, `def.14.AssociatedTypeAstMembership@L49491`, `def.14.AssociatedTypeClassAbstractDefaulted@L49505`
- `def.14.AssocTypeItemsAndNames@L49518`, `def.14.AssocTypeDefault@L49532`, `def.14.ImplAssocType@L49546`, `def.14.AbstractAssociatedTypeNames@L49560`, `def.14.AssocTypeBinding@L49573`, `def.14.AssocTypeBindsPredicate@L49588`, `req.14.GenericParametersAssociatedTypesSupplySites@L49603`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49616`
- `req.14.ImplementationAssociatedTypeBoundForm@L49629`, `def.14.AssociatedTypeLookupOrder@L49642`, `rule.14.T-Alias-Equiv@L49659`, `req.14.AssociatedTypesNoRuntimeSemantics@L49677`, `req.14.AssociatedTypeErasureLowering@L49692`, `diag.14.AssociatedTypes@L49707`, `grammar.14.DynamicClassObjects@L49724`, `req.14.DynamicMethodCallSurfaceSyntax@L49740`
- `rule.14.Parse-Dynamic-Type@L49755`, `req.14.DynamicCastUsesOrdinaryCastParsing@L49771`, `def.14.TypeDynamicAst@L49786`, `def.14.DynamicClassLayoutFields@L49799`, `def.14.DynamicClassRuntimeValue@L49813`, `def.14.SelfOccurs@L49826`, `def.14.DynamicDispatchEligibility@L49853`, `rule.14.WF-Dynamic@L49871`
- `rule.14.WF-Dynamic-Err@L49887`, `rule.14.T-Equiv-Dynamic@L49903`, `rule.14.T-Dynamic-Form@L49919`, `rule.14.Dynamic-NonDispatchable@L49935`, `def.14.LookupMethod@L49951`, `rule.14.T-Dynamic-MethodCall@L49966`, `rule.14.LookupClassMethod-NotFound@L49982`, `req.14.DynamicDispatchDispatchableClassesOnly@L49998`
- `def.14.DynamicValueType@L50013`, `rule.14.Eval-Dynamic-Form@L50026`, `rule.14.Eval-Dynamic-Form-Ctrl@L50042`, `def.14.DynamicDispatchSelection@L50058`, `def.14.DynamicMethodTarget@L50073`, `rule.14.Layout-DynamicClass@L50088`, `rule.14.Size-DynamicClass@L50103`, `rule.14.Align-DynamicClass@L50119`
- `rule.14.ABI-Dynamic@L50135`, `def.14.DynamicValueBits@L50151`, `def.14.DynamicDispatchLoweringJudgements@L50164`, `rule.14.DispatchSym-Impl@L50178`, `rule.14.DispatchSym-Default-None@L50194`, `rule.14.DispatchSym-Default-Mismatch@L50210`, `rule.14.VTable-Order@L50226`, `rule.14.VSlot-Entry@L50242`
- `rule.14.Lower-Dynamic-Form@L50258`, `rule.14.Lower-DynCall@L50274`, `rule.14.EmitVTable-Decl@L50290`, `diag.14.DynamicClassObjects@L50308`, `grammar.14.OpaqueTypes@L50325`, `req.14.OpaqueTypesComposeAsTypeForms@L50340`, `rule.14.Parse-Opaque-Type@L50355`, `def.14.TypeOpaqueAst@L50373`
- `def.14.TypeOpaqueForm@L50386`, `rule.14.WF-Opaque@L50401`, `rule.14.WF-Opaque-Err@L50417`, `rule.14.T-Equiv-Opaque@L50433`, `rule.14.T-Opaque-Return@L50449`, `rule.14.T-Opaque-Project@L50465`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50481`, `req.14.OpaqueTypesNoRuntimeWrapper@L50496`
- `req.14.OpaqueTypesLowerAsConcrete@L50511`, `diag.14.OpaqueTypes@L50526`, `grammar.14.RefinementTypes@L50543`, `req.14.RefinementSelfBinding@L50560`, `rule.14.Parse-RefinementOpt-None@L50575`, `rule.14.Parse-RefinementOpt-Yes@L50591`, `rule.14.ParsePredicateExpr@L50607`, `def.14.TypeRefineAst@L50622`
- `def.14.TypeRefineForm@L50635`, `def.14.PredicateEquiv@L50648`, `rule.14.T-Equiv-Refine@L50663`, `rule.14.T-Equiv-Refine-Norm@L50679`, `rule.14.WF-Refine-Type@L50695`, `rule.14.T-Refine-Intro@L50711`, `rule.14.T-Refine-Elim@L50727`, `rule.14.RefinementSubtypeBase@L50743`
- `rule.14.RefinementSubtypeImplication@L50758`, `req.14.RefinementDecidablePredicateFragment@L50773`, `req.14.RefinementStaticDefaultDynamicFallback@L50786`, `req.14.RefinementRuntimeRepresentationAndPanic@L50801`, `rule.14.LLVMTy-Refine@L50816`, `req.14.RefinementRuntimeCheckLowering@L50832`, `diag.14.RefinementTypes@L50847`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L50864`
- `req.14.CapabilityClassNoFeatureSpecificParser@L50879`, `def.14.CapClassSet@L50894`, `def.14.CapType@L50907`, `def.14.FileSystemInterface@L50920`, `def.14.NetworkInterface@L50951`, `def.14.HeapAllocatorInterface@L50967`, `def.14.FileKindDecl@L50985`, `def.14.IoErrorDecl@L51003`
- `def.14.DirEntryDecl@L51024`, `def.14.AllocationErrorDecl@L51042`, `def.14.ContextDecl@L51059`, `def.14.SystemDecl@L51085`, `def.14.ExecutionDomainSupportDecls@L51109`, `def.14.ReactorDecl@L51128`, `def.14.CapMethodSig@L51148`, `def.14.CapRecv@L51165`
- `def.14.CapabilityLoweringSupport@L51181`, `req.14.CapabilityClassesOrdinaryClasses@L51198`, `req.14.CapabilityClassesGenericBounds@L51211`, `req.14.CapabilityClassNamesReserved@L51224`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51237`, `rule.14.AllocRaw-Unsafe-Err@L51250`, `rule.14.DeallocRaw-Unsafe-Err@L51266`, `def.14.BuiltinTypesFS@L51282`
- `def.14.BuiltinDeclLookup@L51295`, `def.14.BuiltinTypeEnvironment@L51314`, `def.14.BuiltInContext@L51334`, `def.14.ContextBundleFieldType@L51347`, `def.14.ContextBundleType@L51367`, `def.14.ContextBundleFieldValue@L51381`, `def.14.ContextDomainValue@L51401`, `def.14.ContextBundleBuild@L51414`
- `def.14.AllocErrorVal@L51430`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51445`, `req.14.CapabilityBuiltinMethodLowering@L51460`, `diag.14.CapabilityClasses@L51475`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51492`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51507`, `def.14.FoundationalClassName@L51522`, `def.14.FoundationalJudgements@L51535`
- `def.14.HasCloneDropMethod@L51551`, `def.14.CloneDropTypePredicates@L51565`, `def.14.FoundationalImplementationPredicates@L51579`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51599`, `rule.14.BitcopyDrop-Ok@L51612`, `rule.14.BitcopyDrop-Conflict@L51628`, `def.14.BitcopyType@L51644`, `def.14.BitcopyTypeCore@L51657`
- `def.14.BuiltinBitcopyType@L51680`, `def.14.BuiltinDropCloneType@L51711`, `def.14.BuiltinFoundationalClassSignatures@L51725`, `req.14.EqLaws@L51744`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L51757`, `req.14.IteratorNextContract@L51770`, `req.14.StepPartialInverseContract@L51783`, `req.14.DropCloneDynamicSemantics@L51798`
- `req.14.HasherDynamicSemantics@L51811`, `req.14.IntegerStepDynamicSemantics@L51824`, `req.14.CharStepDynamicSemantics@L51837`, `req.14.FoundationalIntrinsicCallLowering@L51852`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L51865`, `diag.14.FoundationalClasses@L51880`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L51895`, `diag-table.14.RefinementPolymorphismDiagnostics@L51908`

#### `spec.procedures-contracts`

Count: 283 total; 283 required; 0 recommended; 0 informative. Ledger line span: L51972-L56430.

- `grammar.15.ProcedureDeclarations@L51972`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L51990`, `rule.15.Parse-Procedure@L52005`, `rule.15.Parse-Signature@L52021`, `rule.15.Parse-ParamList-Empty@L52037`, `rule.15.Parse-ParamList-Cons@L52053`, `rule.15.Parse-Param@L52069`, `rule.15.Parse-ParamMode-None@L52085`
- `rule.15.Parse-ParamMode-Move@L52101`, `rule.15.Parse-ParamTail-End@L52117`, `rule.15.Parse-ParamTail-TrailingComma@L52133`, `rule.15.Parse-ParamTail-Comma@L52149`, `rule.15.Parse-ReturnOpt-None@L52165`, `rule.15.Parse-ReturnOpt-Arrow@L52181`, `def.15.ProcedureDeclAst@L52199`, `def.15.ParamAst@L52212`
- `def.15.ParamNamesAndBinds@L52225`, `def.15.ProcReturn@L52239`, `def.15.BodyReturnType@L52254`, `def.15.ExplicitReturn@L52269`, `def.15.ReturnAnnOk@L52284`, `rule.15.WF-ProcedureDecl@L52297`, `def.15.DeclTyping@L52313`, `def.15.ProvBindCheck@L52328`
- `def.15.DeclTypingItem@L52341`, `rule.15.ProcedureDeclOkJudgement@L52363`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52376`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52392`, `req.15.ExportedProcedureForeignCallableObligations@L52408`, `def.15.MainEntryPointDefinitions@L52421`, `rule.15.Main-Ok@L52442`, `rule.15.Main-Bypass-NonExecutable@L52458`
- `rule.15.Main-Multiple@L52474`, `rule.15.Main-Generic-Err@L52490`, `rule.15.Main-Signature-Err@L52506`, `rule.15.Main-Missing@L52522`, `def.15.MainDiagRefs@L52538`, `def.15.FuncValDefined@L52553`, `def.15.BindParams@L52566`, `def.15.ArgumentPassingJudgements@L52579`
- `def.15.CallJudgements@L52593`, `def.15.CallTargets@L52606`, `def.15.BuiltinProcedureParams@L52622`, `def.15.SynthParams@L52636`, `def.15.CalleeProc@L52649`, `def.15.CallParams@L52663`, `def.15.ReturnOut@L52679`, `rule.15.EvalArgsSigma-Empty@L52698`
- `rule.15.EvalArgsSigma-Cons-Move@L52713`, `rule.15.EvalArgsSigma-Cons-Ref@L52729`, `rule.15.EvalArgsSigma-Ctrl-Move@L52745`, `rule.15.EvalArgsSigma-Ctrl-Ref@L52761`, `rule.15.ApplyRegionProc-NewScoped@L52777`, `rule.15.ApplyRegionProc-Alloc@L52793`, `rule.15.ApplyRegionProc-Reset@L52809`, `rule.15.ApplyRegionProc-Freeze@L52825`
- `rule.15.ApplyRegionProc-Thaw@L52841`, `rule.15.ApplyRegionProc-Free@L52857`, `rule.15.ApplyCancelProc-New@L52873`, `rule.15.ApplyProcSigma@L52889`, `rule.15.EvalSigma-Call-Proc@L52905`, `rule.15.CG-Item-Procedure@L52923`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L52939`, `diag.15.ProcedureDeclarations@L52954`
- `grammar.15.MethodsAndReceivers@L52971`, `req.15.ClassAndStateMethodsReuseReceiverForms@L52988`, `rule.15.Parse-MethodDefAfterVis@L53003`, `rule.15.Parse-Override-Yes@L53019`, `rule.15.Parse-Override-No@L53035`, `rule.15.Parse-MethodSignature@L53051`, `rule.15.Parse-StateMethodSignature-Receiver@L53067`, `rule.15.Parse-MethodParams-None@L53083`
- `rule.15.Parse-MethodParams-Comma@L53099`, `rule.15.Parse-Receiver-Short-Const@L53115`, `rule.15.Parse-Receiver-Short-Unique@L53131`, `rule.15.Parse-Receiver-Short-Shared@L53147`, `rule.15.Parse-Receiver-Explicit@L53163`, `def.15.MethodDeclAst@L53181`, `def.15.ReceiverAst@L53194`, `def.15.RecordFieldsMethodsAndSelf@L53209`
- `def.15.SelfType@L53224`, `def.15.RecvType@L53237`, `def.15.RecvMode@L53253`, `def.15.RecvPerm@L53267`, `def.15.MethodSignaturesAndParams@L53282`, `rule.15.Recv-Explicit@L53301`, `rule.15.Record-Method-RecvSelf-Err@L53317`, `rule.15.Recv-Const@L53333`
- `rule.15.Recv-Unique@L53348`, `rule.15.Recv-Shared@L53363`, `rule.15.WF-Record-Method@L53378`, `rule.15.T-Record-Method-Body@L53394`, `rule.15.WF-Record-Methods@L53410`, `rule.15.Record-Method-Dup@L53426`, `def.15.ArgsOkJudg@L53442`, `def.15.RecvBaseType@L53455`
- `rule.15.Args-Empty@L53468`, `rule.15.Args-Cons@L53483`, `rule.15.Args-Cons-Ref@L53499`, `def.15.RecvArgOk@L53515`, `rule.15.T-Record-MethodCall@L53528`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53544`, `def.15.RecvArgMode@L53559`, `def.15.MethodOf@L53573`
- `def.15.RecvBase@L53588`, `def.15.RecvParams@L53601`, `rule.15.EvalRecvSigma-Move@L53616`, `rule.15.EvalRecvSigma-Ref-Dyn@L53632`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L53648`, `rule.15.EvalRecvSigma-Ref@L53664`, `rule.15.EvalRecvSigma-Ctrl-Move@L53680`, `rule.15.EvalRecvSigma-Ctrl-Ref@L53696`
- `def.15.BindMethodParams@L53712`, `rule.15.ApplyMethodSigma-Prim@L53725`, `rule.15.ApplyMethodSigma@L53741`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L53759`, `rule.15.Mangle-Record-Method@L53772`, `rule.15.Mangle-Class-Method@L53788`, `rule.15.Mangle-State-Method@L53804`, `diag.15.MethodsAndReceivers@L53822`
- `req.15.OverloadingNoAdditionalSyntax@L53839`, `req.15.OverloadResolutionNotParserConcern@L53854`, `def.15.ClassDefaults@L53869`, `def.15.LookupMethod@L53882`, `rule.15.LookupMethod-NotFound@L53899`, `rule.15.LookupMethod-Ambig@L53915`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L53931`, `req.15.FreeCallOverloadResolutionAlgorithm@L53944`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L53966`, `req.15.NoRuntimeOverloadSearch@L53981`, `req.15.OverloadResolutionCompleteBeforeLowering@L53996`, `diag-table.15.Overloading@L54011`, `diag.15.MethodLookupDiagnostics@L54028`, `grammar.15.ContractClauses@L54045`, `req.15.ForeignContractStartDisambiguatesContracts@L54067`, `rule.15.Parse-ContractClauseOpt-None@L54080`
- `rule.15.Parse-ContractClauseOpt-Yes@L54096`, `rule.15.Parse-ContractBody-PostOnly@L54112`, `rule.15.Parse-ContractBody-PrePost@L54128`, `rule.15.Parse-ContractBody-PreOnly@L54144`, `def.15.ContractClauseAst@L54162`, `def.15.ContractOpt@L54175`, `rule.15.WF-Contract@L54190`, `def.15.ContractPurityJudgementIntro@L54207`
- `rule.15.Pure-Literal@L54220`, `rule.15.Pure-Ident@L54236`, `rule.15.Pure-Field@L54252`, `rule.15.Pure-Tuple-Access@L54268`, `rule.15.Pure-Index@L54284`, `rule.15.Pure-Unary@L54300`, `rule.15.Pure-Binary@L54316`, `def.15.PureOps@L54332`
- `rule.15.Pure-Cast@L54345`, `rule.15.Pure-If@L54361`, `rule.15.Pure-If-Is@L54377`, `rule.15.Pure-If-Is-No-Else@L54393`, `rule.15.Pure-If-Case@L54409`, `rule.15.Pure-If-Case-No-Else@L54425`, `rule.15.Pure-Block@L54441`, `rule.15.Pure-Tuple@L54457`
- `rule.15.Pure-Array@L54473`, `rule.15.Pure-Record@L54489`, `rule.15.Pure-Call-Builtin@L54505`, `rule.15.Pure-Call-Procedure@L54521`, `rule.15.Pure-Method-Const@L54537`, `rule.15.Pure-Comptime@L54553`, `def.15.ContractPurityHelperPredicates@L54569`, `req.15.ContractNeverPureForms@L54589`
- `def.15.PreconditionEvaluationContext@L54602`, `def.15.PostconditionEvaluationContext@L54615`, `req.15.ContractClausesNoIndependentRuntimeEffect@L54630`, `req.15.ContractClauseLoweringViaVerificationResults@L54645`, `diag.15.ContractClauses@L54660`, `req.15.PreconditionSyntaxDefinition@L54677`, `req.15.PreconditionsParsedByContractBody@L54692`, `def.15.PreconditionOf@L54707`
- `def.15.PreconditionProofContext@L54724`, `rule.15.Pre-Satisfied@L54740`, `def.15.PreconditionElisionRules@L54756`, `req.15.CallerResponsibleForPrecondition@L54776`, `req.15.PreconditionRuntimeEvaluationOrder@L54791`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L54806`, `diag.15.Preconditions@L54821`, `grammar.15.Postconditions@L54838`
- `rule.15.Parse-Contract-Result@L54856`, `rule.15.Parse-Contract-Entry@L54872`, `def.15.ContractIntrinsicAst@L54890`, `def.15.PostconditionOf@L54903`, `def.15.PostconditionProofContext@L54920`, `rule.15.Post-Valid@L54935`, `def.15.PostconditionElisionRules@L54951`, `req.15.ContractResultProperties@L54971`
- `rule.15.Result-Union-Type@L54988`, `rule.15.Result-Is-Predicate@L55004`, `rule.15.Result-Narrowing@L55020`, `rule.15.Propagate-Postcondition@L55036`, `rule.15.Result-Modal@L55052`, `rule.15.Result-Generic@L55068`, `rule.15.Result-Generic-Constraint@L55084`, `req.15.ContractEntryConstraints@L55100`
- `rule.15.Entry-Type@L55118`, `req.15.PostconditionResultRuntimeBinding@L55136`, `req.15.ContractEntryRuntimeCapture@L55149`, `def.15.EntryCaptureTiming@L55166`, `rule.15.EntryCapturePhase@L55186`, `def.15.EntryCaptureValue@L55203`, `req.15.PostconditionLoweringRepresentation@L55218`, `diag.15.Postconditions@L55233`
- `grammar.15.Invariants@L55250`, `rule.15.Parse-InvariantOpt-None@L55268`, `rule.15.Parse-InvariantOpt-Yes@L55284`, `rule.15.ParseLoopInvariantOpt@L55300`, `def.15.InvariantAst@L55315`, `def.15.TypeInvariantAstExtensions@L55329`, `def.15.LoopInvariantAstPreservation@L55344`, `def.15.TypeInvariantContext@L55359`
- `def.15.TypeInvariantEnforcementPoints@L55376`, `req.15.TypeInvariantsForbidPublicMutableFields@L55393`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55406`, `def.15.LoopInvariantEnforcementPoints@L55419`, `req.15.LoopInvariantExitFact@L55436`, `req.15.InvariantVerificationModeRules@L55449`, `req.15.InvariantRuntimeChecks@L55464`, `req.15.InvariantLoweringViaVerificationLogic@L55479`
- `diag.15.Invariants@L55494`, `req.15.VerificationLogicNoSurfaceSyntax@L55511`, `req.15.VerificationLogicNotParserOwned@L55526`, `def.15.ContractKind@L55541`, `def.15.VerificationFact@L55554`, `def.15.CheckState@L55567`, `def.15.ContractCheck@L55580`, `def.15.DynamicScopeAndContext@L55595`
- `rule.15.Contract-Static-OK@L55616`, `rule.15.Contract-Static-Fail@L55632`, `rule.15.Contract-Dynamic-Elide@L55648`, `rule.15.Contract-Dynamic-Check@L55664`, `req.15.MandatoryProofTechniques@L55680`, `def.15.ProofContextAt@L55700`, `def.15.DecidablePredicates@L55721`, `rule.15.Ent-True@L55741`
- `rule.15.Ent-Fact@L55757`, `rule.15.Ent-And@L55773`, `rule.15.Ent-Or-L@L55789`, `rule.15.Ent-Or-R@L55805`, `rule.15.Ent-Linear@L55821`, `def.15.LinearIntegerEntailment@L55837`, `req.15.LinearEntailmentSoundAndComplete@L55860`, `def.15.StaticProofAt@L55873`
- `def.15.NegFact@L55886`, `req.15.VerificationFactsNoRuntimeRepresentation@L55908`, `rule.15.Fact-Dominate@L55925`, `req.15.FactGeneration@L55941`, `req.15.TypeNarrowingFromFacts@L55964`, `def.15.ContractEnvironments@L55979`, `rule.15.Check-True@L55996`, `rule.15.Check-False@L56012`
- `rule.15.Check-Panic@L56028`, `rule.15.Check-Ok@L56044`, `rule.15.Check-Fail@L56060`, `req.15.DynamicChecksInjectFacts@L56076`, `def.15.RuntimeCheckInsertionPointsIntro@L56091`, `rule.15.Insert-Precondition-Check@L56104`, `rule.15.Insert-Postcondition-Check@L56120`, `rule.15.Insert-TypeInv-Construction-Check@L56136`
- `rule.15.Insert-TypeInv-PreCall-Check@L56152`, `rule.15.Insert-TypeInv-PostCall-Check@L56168`, `rule.15.Insert-LoopInv-Init-Check@L56184`, `rule.15.Insert-LoopInv-Maintenance-Check@L56200`, `rule.15.Insert-Refinement-Check@L56216`, `diag.15.VerificationLogic@L56234`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56251`, `req.15.BehavioralSubtypingNotParserOwned@L56266`
- `def.15.BehavioralSubtypingRelationship@L56281`, `req.15.BehavioralSubtypingLiskovRequirement@L56296`, `req.15.BehavioralSubtypingPreconditionRule@L56309`, `req.15.BehavioralSubtypingPostconditionRule@L56325`, `req.15.BehavioralSubtypingVerificationStrategy@L56341`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56357`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56372`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56387`
- `diag.15.BehavioralSubtyping@L56402`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56417`, `diag-table.15.ProcedureContractEntryDiagnostics@L56430`

### Language Constructs, Dynamic Semantics, And Feature Semantics

#### `spec.expressions`

Count: 478 total; 475 required; 0 recommended; 0 informative. Ledger line span: L56475-L64152.

- `grammar.16.LiteralAndNameExpressions@L56475`, `req.16.QualifiedApplicationOwnership@L56493`, `rule.16.Parse-Literal-Expr@L56508`, `rule.16.Parse-Null-Ptr@L56524`, `rule.16.Parse-Identifier-Expr@L56540`, `rule.16.Parse-Qualified-Name@L56556`, `def.16.LiteralKindAndToken@L56574`, `def.16.LiteralNameExprAst@L56588`
- `def.16.QualifiedNameResolution@L56602`, `def.16.ValuePathType@L56620`, `def.16.NumericLiteralTypeSets@L56643`, `def.16.NumericLiteralParsingHelpers@L56660`, `rule.16.T-Int-Literal-Suffix@L56687`, `rule.16.T-Int-Literal-Default@L56703`, `rule.16.T-Float-Literal-Explicit@L56719`, `rule.16.T-Float-Literal-Infer@L56735`
- `rule.16.T-Bool-Literal@L56751`, `rule.16.T-Char-Literal@L56767`, `rule.16.T-String-Literal@L56783`, `rule.16.Syn-Literal@L56799`, `def.16.NullLiteralExpected@L56815`, `rule.16.Chk-Int-Literal@L56828`, `rule.16.Chk-Float-Literal-Explicit@L56844`, `rule.16.Chk-Float-Literal-Infer@L56860`
- `rule.16.Chk-Null-Literal@L56876`, `def.16.PtrNullExpected@L56892`, `rule.16.Chk-Null-Ptr@L56905`, `rule.16.Syn-PtrNull-Err@L56921`, `rule.16.Chk-PtrNull-Err@L56937`, `rule.16.T-Ident@L56953`, `rule.16.T-Path-Value@L56969`, `rule.16.Expr-Unresolved-Err@L56985`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57001`, `def.16.EvaluationJudgements@L57016`, `def.16.LiteralRuntimeValues@L57031`, `rule.16.EvalSigma-Literal@L57052`, `rule.16.EvalSigma-PtrNull@L57068`, `rule.16.EvalSigma-Ident@L57083`, `rule.16.EvalSigma-Path@L57099`, `rule.16.EvalSigma-ErrorExpr@L57115`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57130`, `rule.16.Lower-Expr-Literal@L57145`, `rule.16.Lower-Expr-PtrNull@L57161`, `rule.16.Lower-Expr-Ident-Local@L57176`, `rule.16.Lower-Expr-Ident-Path@L57192`, `rule.16.Lower-Expr-Path@L57208`, `rule.16.Lower-Expr-Error@L57223`, `diag.16.LiteralAndNameExpressions@L57240`
- `grammar.16.AccessAndPlaceExpressions@L57257`, `req.16.AccessPostfixOwnership@L57273`, `rule.16.Postfix-Field@L57288`, `rule.16.Postfix-TupleIndex@L57304`, `rule.16.Postfix-Index@L57320`, `def.16.IsPlace@L57336`, `rule.16.Parse-Place-Deref@L57349`, `rule.16.Parse-Place-Postfix@L57365`
- `rule.16.Parse-Place-Err@L57381`, `def.16.AccessPlaceAst@L57399`, `def.16.PlaceForms0@L57412`, `def.16.FieldVisibility@L57425`, `def.16.IndexClassification@L57439`, `rule.16.T-Field-Record@L57456`, `rule.16.T-Field-Record-Perm@L57472`, `rule.16.P-Field-Record@L57488`
- `rule.16.P-Field-Record-Perm@L57504`, `rule.16.T-Tuple-Index@L57520`, `rule.16.T-Tuple-Index-Perm@L57536`, `rule.16.P-Tuple-Index@L57552`, `rule.16.P-Tuple-Index-Perm@L57568`, `rule.16.T-Index-Array@L57584`, `rule.16.T-Index-Array-Dynamic@L57600`, `rule.16.T-Index-Array-Perm@L57616`
- `rule.16.T-Index-Array-Perm-Dynamic@L57632`, `rule.16.T-Index-Slice@L57648`, `rule.16.T-Index-Slice-Perm@L57664`, `rule.16.T-Slice-From-Array@L57680`, `rule.16.T-Slice-From-Array-Perm@L57696`, `rule.16.T-Slice-From-Slice@L57712`, `rule.16.T-Slice-From-Slice-Perm@L57728`, `rule.16.PlaceIndexAndSliceCounterparts@L57744`
- `rule.16.Coerce-Array-Slice@L57757`, `rule.16.Union-DirectAccess-Err@L57773`, `rule.16.ValueUse-NonBitcopyPlace@L57789`, `rule.16.EvalSigma-FieldAccess@L57807`, `rule.16.EvalSigma-TupleAccess@L57823`, `rule.16.EvalSigma-Index@L57839`, `rule.16.EvalSigma-Index-Range@L57855`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L57871`
- `rule.16.Lower-Expr-FieldAccess@L57886`, `rule.16.Lower-Expr-TupleAccess@L57902`, `rule.16.Lower-Expr-IndexFamily@L57918`, `rule.16.Lower-Place-Ident@L57931`, `rule.16.Lower-Place-Field@L57946`, `rule.16.Lower-Place-Tuple@L57962`, `rule.16.Lower-Place-Index@L57978`, `rule.16.Lower-Place-Deref@L57994`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58010`, `diag.16.AccessAndPlaceExpressions@L58025`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58038`, `grammar.16.CallExpressions@L58055`, `req.16.QualifiedApplyParenPreResolution@L58074`, `rule.16.Postfix-Call@L58089`, `rule.16.Postfix-Call-TypeArgs@L58105`, `rule.16.Postfix-MethodCall@L58121`
- `rule.16.Parse-Qualified-Apply-Paren@L58137`, `rule.16.ArgumentListParsingFamily@L58153`, `def.16.ArgAst@L58168`, `def.16.CallExprAst@L58181`, `def.16.ArgAccessors@L58194`, `def.16.MovedArg@L58208`, `req.16.QualifiedParenthesizedApplicationResolution@L58223`, `def.16.CallStaticJudgementsAndArgumentTyping@L58242`
- `rule.16.ArgsT-Empty@L58270`, `rule.16.ArgsT-Cons@L58285`, `rule.16.ArgsT-Cons-Ref@L58301`, `rule.16.T-Call-Generic-Infer@L58317`, `rule.16.T-Call@L58333`, `rule.16.Call-Callee-NotFunc@L58349`, `rule.16.Call-ArgCount-Err@L58365`, `rule.16.Call-ArgType-Err@L58381`
- `rule.16.Call-Move-Missing@L58397`, `rule.16.Call-Move-Unexpected@L58413`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58429`, `rule.16.Call-Arg-NotPlace@L58445`, `rule.16.Chk-Call-Generic-Infer@L58461`, `req.16.CallTypeArgsStaticOwnership@L58477`, `req.16.MethodRecordClosureCallStaticOwnership@L58490`, `req.16.ExternProcedureCallsRequireUnsafe@L58503`
- `rule.16.EvalSigma-Call-Closure@L58518`, `rule.16.EvalSigma-Call-RegionProc@L58534`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58550`, `rule.16.EvalSigma-Call-CancelProc@L58566`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58582`, `rule.16.EvalSigma-Call-Proc@L58598`, `rule.16.EvalSigma-Call-Record@L58614`, `rule.16.EvalSigma-MethodCall@L58630`
- `req.16.CallControlPropagation@L58646`, `req.16.MethodCallControlPropagation@L58659`, `req.16.CallTypeArgsEvaluationElaboration@L58672`, `rule.16.Lower-Args-Empty@L58687`, `rule.16.Lower-Args-Cons-Move@L58702`, `rule.16.Lower-Args-Cons-Ref@L58718`, `rule.16.Lower-Expr-Call-Closure@L58734`, `rule.16.Lower-Expr-CallFamily@L58750`
- `rule.16.Lower-MethodCallFamily@L58763`, `req.16.CallTypeArgsLoweringElaboration@L58776`, `diag.16.CallExpressions@L58791`, `grammar.16.OperatorExpressions@L58808`, `req.16.OperatorPrefixSyntaxOwnership@L58839`, `rule.16.ParseRangeFamily@L58854`, `rule.16.ParseLeftChainFamily@L58867`, `rule.16.ParsePowerFamily@L58880`
- `rule.16.Parse-Unary-Prefix@L58893`, `def.16.RangeAndOperatorExprAst@L58911`, `def.16.OperatorSets@L58925`, `def.16.OperatorStaticTypes@L58943`, `rule.16.T-Range-Lift@L58958`, `rule.16.RangeTypingFamily@L58974`, `rule.16.T-Not-Bool@L58987`, `rule.16.T-Not-Int@L59003`
- `rule.16.T-Neg@L59019`, `rule.16.T-Arith@L59035`, `rule.16.T-Bitwise@L59051`, `rule.16.T-Shift@L59067`, `rule.16.T-Compare-Eq@L59083`, `rule.16.T-Compare-Ord@L59099`, `rule.16.T-Logical@L59115`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59133`
- `def.16.OperatorComparisonRuntime@L59155`, `def.16.OperatorBitShiftArithmeticRuntime@L59175`, `def.16.UnaryOperatorRuntime@L59195`, `req.16.FloatUnaryNegationTotality@L59213`, `def.16.BinaryOperatorRuntime@L59226`, `rule.16.EvalSigma-Range@L59247`, `rule.16.EvalSigma-Unary@L59263`, `rule.16.EvalSigma-Bin-And-False@L59279`
- `rule.16.EvalSigma-Bin-And-True@L59295`, `rule.16.EvalSigma-Bin-Or-True@L59311`, `rule.16.EvalSigma-Bin-Or-False@L59327`, `rule.16.EvalSigma-Binary@L59343`, `req.16.OperatorUndefinedAndNaNBehavior@L59359`, `rule.16.Lower-Expr-Unary@L59374`, `rule.16.Lower-Expr-Bin-And@L59390`, `rule.16.Lower-Expr-Bin-Or@L59406`
- `rule.16.Lower-Expr-Binary@L59422`, `rule.16.Lower-Expr-Range@L59438`, `def.16.UnaryOperatorLoweringPanicCheck@L59454`, `rule.16.Lower-UnOp-Ok@L59468`, `rule.16.Lower-UnOp-Panic@L59484`, `req.16.UnaryNegationLoweringOverflowChecks@L59500`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59513`, `diag.16.OperatorExpressions@L59528`
- `grammar.16.CastAndTransmuteExpressions@L59545`, `req.16.WidenPrefixOwnershipForCastTransmute@L59561`, `rule.16.Parse-Cast@L59576`, `rule.16.Parse-CastTail-None@L59592`, `rule.16.Parse-CastTail-As@L59608`, `rule.16.ParseTransmuteExprFamily@L59624`, `req.16.WidenParsingOwnershipForCastTransmute@L59637`, `def.16.CastTransmuteExprAst@L59652`
- `req.16.WidenAstOwnershipForCastTransmute@L59665`, `def.16.CastValidity@L59680`, `rule.16.T-Cast@L59695`, `rule.16.T-Cast-Invalid@L59711`, `rule.16.T-Transmute-SizeEq@L59727`, `rule.16.T-Transmute-AlignEq@L59743`, `rule.16.T-Transmute@L59759`, `rule.16.Transmute-Unsafe-Err@L59775`
- `def.16.ValidTransmuteTarget@L59791`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L59808`, `def.16.CastDynamicContext@L59823`, `def.16.CastRuntimeConversionHelpers@L59837`, `rule.16.CastVal-Id@L59871`, `rule.16.CastVal-Int-Int-Signed@L59887`, `rule.16.CastVal-Int-Int-Unsigned@L59903`, `rule.16.CastVal-Int-Float@L59919`
- `req.16.IntToFloatLoweringPreservesSignedness@L59935`, `rule.16.CastVal-Float-Float@L59948`, `rule.16.CastVal-Float-Int@L59964`, `rule.16.CastVal-Bool-Int@L59980`, `rule.16.CastVal-Int-Bool@L59998`, `rule.16.CastVal-Char-U32@L60016`, `rule.16.CastVal-U32-Char@L60032`, `rule.16.EvalSigma-Cast@L60048`
- `rule.16.EvalSigma-Cast-Panic@L60064`, `def.16.TransmuteVal@L60080`, `rule.16.EvalSigma-Transmute@L60093`, `rule.16.EvalSigma-Transmute-Ctrl@L60109`, `req.16.WidenDynamicOwnershipForCastTransmute@L60125`, `rule.16.Lower-Expr-Cast@L60140`, `rule.16.Lower-Expr-Transmute@L60156`, `rule.16.LowerCastTransmuteFamily@L60172`
- `diag.16.CastAndTransmuteExpressions@L60187`, `grammar.16.ConstructionExpressions@L60204`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60226`, `rule.16.Parse-Tuple-Literal@L60241`, `rule.16.Parse-Array-Segment-Elem@L60257`, `rule.16.Parse-Array-Segment-Repeat@L60273`, `rule.16.Parse-Array-Segment-List-Empty@L60289`, `rule.16.Parse-Array-Segment-List-Single@L60304`
- `rule.16.Parse-Array-Segment-List-Comma@L60320`, `rule.16.Parse-Array-Literal@L60336`, `rule.16.Parse-Record-Literal-ModalState@L60352`, `rule.16.Parse-Record-Literal@L60368`, `rule.16.Parse-Qualified-Apply-Brace@L60384`, `rule.16.ConstructionListAndShorthandParsingFamily@L60400`, `def.16.FieldInitAst@L60415`, `def.16.ConstructionExprAst@L60428`
- `def.16.FieldInitNamesAndSet@L60441`, `req.16.QualifiedBraceApplicationResolution@L60455`, `req.16.QualifiedParenApplicationConstructionResolution@L60471`, `rule.16.T-Unit-Literal@L60486`, `rule.16.T-Tuple-Literal@L60501`, `def.16.ArraySegmentLength@L60517`, `rule.16.T-Array-Literal-Segments@L60531`, `def.16.RecordFieldNameSet@L60556`
- `rule.16.T-Record-Literal@L60570`, `rule.16.Record-FieldInit-Dup@L60586`, `rule.16.Record-FieldInit-Missing@L60602`, `rule.16.RecordFieldUnknownNotVisibleFamily@L60618`, `rule.16.Record-Field-NonBitcopy-Move@L60631`, `rule.16.EnumLiteralTypingFamily@L60647`, `def.16.RecordDefaultConstructionEligibility@L60660`, `rule.16.T-Record-Default@L60674`
- `rule.16.Record-Default-Init-Err@L60690`, `rule.16.EvalSigmaTupleConstructionFamily@L60708`, `rule.16.EvalSigmaArrayConstructionFamily@L60721`, `rule.16.EvalSigmaRecordConstructionFamily@L60734`, `rule.16.EvalSigmaEnumConstructionFamily@L60747`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L60760`, `rule.16.Lower-Expr-Tuple@L60775`, `rule.16.Lower-Expr-Array@L60791`
- `rule.16.Lower-Expr-Record@L60807`, `rule.16.LowerEnumConstructionFamily@L60823`, `rule.16.Lower-CallIR-RecordCtor@L60836`, `diag.16.ConstructionExpressions@L60854`, `grammar.16.ControlExpressions@L60871`, `req.16.ControlExpressionOwnership@L60895`, `rule.16.Parse-If-Expr@L60911`, `rule.16.Parse-If-Is-Single@L60927`
- `rule.16.Parse-If-Is-CaseList@L60943`, `rule.16.Parse-Loop-Expr@L60959`, `rule.16.Parse-Block-Expr@L60975`, `rule.16.ControlExpressionParsingRemainderFamily@L60991`, `def.16.ControlExprAst@L61006`, `def.16.ControlAstHelpers@L61019`, `def.16.LoopTypeInference@L61033`, `req.16.BlockTypingOwnershipForControlExpressions@L61057`
- `rule.16.T-If@L61076`, `rule.16.T-If-No-Else@L61092`, `rule.16.CheckIfFamily@L61108`, `req.16.PatternTypingOwnershipForControlExpressions@L61121`, `rule.16.T-If-Is@L61137`, `rule.16.T-If-Is-No-Else@L61153`, `rule.16.IfCaseTypingFamily@L61169`, `rule.16.CheckIfIsAndIfCaseFamily@L61182`
- `req.16.LoopInvariantTypingOwnership@L61196`, `rule.16.T-Loop-Infinite@L61209`, `rule.16.T-Loop-Conditional@L61225`, `rule.16.T-Loop-Iter@L61241`, `rule.16.AsyncIteratorLoopTypingFamily@L61257`, `rule.16.EvalSigma-If-True@L61272`, `rule.16.EvalSigma-If-False-None@L61288`, `rule.16.EvalSigma-If-False-Some@L61304`
- `rule.16.EvalSigma-If-Ctrl@L61320`, `rule.16.EvalSigma-If-Is@L61336`, `rule.16.EvalSigma-If-Is-Ctrl@L61352`, `rule.16.EvalSigma-If-Cases@L61368`, `rule.16.EvalSigma-If-Cases-Ctrl@L61384`, `rule.16.EvalIfCasesFamily@L61400`, `rule.16.EvalSigma-Block@L61413`, `def.16.LoopIterableTypePredicates@L61429`
- `def.16.LoopIteratorRuntime@L61447`, `def.16.LoopIterJudgement@L61480`, `rule.16.EvalSigma-Loop-Infinite-Step@L61493`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61509`, `rule.16.EvalSigma-Loop-Infinite-Break@L61525`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61541`, `rule.16.EvalSigma-Loop-Cond-False@L61557`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61573`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61589`, `rule.16.EvalSigma-Loop-Cond-Break@L61605`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L61621`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L61637`, `rule.16.EvalSigma-Loop-Iter@L61653`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L61669`, `rule.16.LoopIter-Done@L61685`, `rule.16.LoopIter-Step-Val@L61701`
- `rule.16.LoopIter-Step-Continue@L61717`, `rule.16.LoopIter-Step-Break@L61733`, `rule.16.LoopIter-Step-Ctrl@L61749`, `rule.16.Lower-Expr-If@L61767`, `rule.16.Lower-Expr-If-Is@L61783`, `rule.16.Lower-Expr-If-Cases@L61799`, `rule.16.LowerLoopExpressionFamily@L61815`, `rule.16.Lower-Expr-Block@L61828`
- `req.16.ControlExpressionLoweringOwnership@L61844`, `diag.16.ControlExpressions@L61859`, `req.16.ControlExpressionDiagnosticOwnership@L61872`, `grammar.16.EffectfulCoreExpressions@L61890`, `req.16.RegionAliasAllocRewrite@L61910`, `rule.16.Parse-Unary-Deref@L61925`, `rule.16.Parse-Unary-AddressOf@L61941`, `rule.16.Parse-Unary-Move@L61957`
- `rule.16.Postfix-Propagate@L61973`, `rule.16.Parse-Alloc-Implicit@L61989`, `rule.16.Parse-Unsafe-Expr@L62005`, `def.16.EffectfulCoreExprAst@L62023`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62036`, `def.16.AddressOfStaticHelpers@L62055`, `rule.16.T-Unsafe-Expr@L62070`, `rule.16.Chk-Unsafe-Expr@L62086`
- `rule.16.T-AddrOf@L62102`, `rule.16.T-Deref-Ptr@L62118`, `rule.16.T-Deref-Raw@L62134`, `rule.16.DerefPlaceTypingFamily@L62150`, `rule.16.T-Move@L62163`, `rule.16.T-Alloc-Explicit@L62179`, `rule.16.T-Alloc-Implicit@L62195`, `def.16.SuccessMember@L62211`
- `rule.16.T-Propagate@L62224`, `def.16.SuccessMemberAsync@L62240`, `rule.16.T-Async-Try@L62253`, `rule.16.Async-Try-Infallible-Err@L62269`, `rule.16.EvalSigma-UnsafeBlock@L62287`, `rule.16.EvalSigma-AddressOf@L62303`, `rule.16.EvalSigma-Deref@L62319`, `rule.16.EvalSigma-Move@L62335`
- `rule.16.EvalSigma-Alloc-Implicit@L62351`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62367`, `rule.16.EvalSigma-Alloc-Explicit@L62383`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62399`, `rule.16.EvalSigma-Propagate-Success@L62415`, `rule.16.EvalSigma-Propagate-Success-Async@L62431`, `rule.16.EvalSigma-Propagate-Error@L62447`, `rule.16.EvalSigma-Propagate-Error-Async@L62463`
- `rule.16.EvalSigma-Propagate-Ctrl@L62480`, `def.16.ExprStateAndTerminalExpr@L62496`, `rule.16.StepSigma-Pure@L62511`, `rule.16.StepSigma-Alloc-Implicit@L62527`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62543`, `rule.16.StepSigma-Alloc-Explicit@L62559`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62575`, `rule.16.StepSigma-Block@L62591`
- `rule.16.StepSigma-UnsafeBlock@L62607`, `rule.16.StepSigma-Loop@L62623`, `rule.16.StepSigma-Stateful-Other@L62639`, `rule.16.Lower-Expr-UnsafeBlock@L62657`, `rule.16.Lower-Expr-Move@L62673`, `rule.16.Lower-Expr-AddressOf@L62689`, `rule.16.Lower-Expr-Deref@L62705`, `rule.16.Lower-Expr-Alloc@L62721`
- `rule.16.Lower-Expr-Propagate-Success@L62737`, `rule.16.Lower-Expr-Propagate-Return@L62753`, `req.16.EffectfulCoreLoweringMechanics@L62769`, `diag.16.EffectfulCoreExpressions@L62784`, `grammar.16.ClosureAndPipelineExpressions@L62801`, `req.16.ClosureParamTrailingComma@L62820`, `req.16.ClosureUnionParamParentheses@L62833`, `req.16.ClosureInvocationOrdinaryCallSyntax@L62846`
- `rule.16.Parse-Pipeline@L62861`, `rule.16.Parse-PipelineTail-Stop@L62877`, `rule.16.Parse-PipelineTail-Cons@L62893`, `rule.16.Parse-Closure-Expr@L62909`, `rule.16.Parse-Closure-Expr-Empty@L62925`, `rule.16.Parse-ClosureParams-Single@L62941`, `rule.16.Parse-ClosureParams-Cons@L62957`, `rule.16.Parse-ClosureParamType-Grouped@L62973`
- `rule.16.Parse-ClosureParamType-Plain@L62989`, `rule.16.Parse-ClosureParam-MoveTyped@L63005`, `rule.16.Parse-ClosureParam-MoveUntyped@L63021`, `rule.16.Parse-ClosureParam-Typed@L63037`, `rule.16.Parse-ClosureParam-Untyped@L63053`, `rule.16.Parse-ClosureRetOpt-Some@L63069`, `rule.16.Parse-ClosureRetOpt-None@L63085`, `rule.16.Parse-ClosureBody-Block@L63101`
- `rule.16.Parse-ClosureBody-Expr@L63117`, `def.16.ClosurePipelineAstForms@L63135`, `def.16.ClosureCaptureSets@L63154`, `def.16.ClosureEscapeClassification@L63174`, `def.16.ClosureParameterAccessors@L63189`, `rule.16.T-Closure-NonCapturing@L63203`, `rule.16.T-Closure-Capturing@L63221`, `rule.16.T-Closure-Escaping@L63240`
- `rule.16.K-Closure-Escape-Type@L63260`, `rule.16.Capture-Const@L63276`, `rule.16.Capture-Shared@L63292`, `rule.16.Capture-Unique-Err@L63308`, `rule.16.T-ClosureCall@L63324`, `rule.16.Infer-Closure-Params@L63340`, `rule.16.Infer-Closure-Params-Err@L63356`, `rule.16.Infer-Closure-Return@L63372`
- `req.16.ClosureSharedDependencyInference@L63388`, `def.16.ClosureCaptureBindingAccessors@L63401`, `rule.16.B-Closure-NonCapturing@L63425`, `rule.16.B-Closure-Capturing@L63441`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63460`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63477`, `rule.16.B-Closure-RefCapture-Moved-Err@L63494`, `rule.16.T-Pipeline@L63511`
- `rule.16.T-Pipeline-NotCallable-Err@L63529`, `rule.16.T-Pipeline-TypeMismatch-Err@L63546`, `rule.16.T-Pipeline-ArgCount-Err@L63564`, `rule.16.B-Pipeline@L63581`, `req.16.ClosureParamInferenceFailure@L63597`, `req.16.ClosureSharedDependencyInferenceRestated@L63610`, `def.16.ClosureEnvironmentRuntimeModel@L63625`, `rule.16.EvalSigma-Closure-NonCapturing@L63661`
- `rule.16.EvalSigma-Closure-Capturing@L63677`, `def.16.MarkMoved@L63695`, `rule.16.EvalSigma-ClosureCall@L63709`, `def.16.ClosureCallRuntimeHelpers@L63727`, `rule.16.EvalSigma-ClosureCall-Ctrl@L63749`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L63765`, `req.16.ClosureCallResolvedInternalFormRuntime@L63782`, `req.16.PipelineDesugaring@L63795`
- `rule.16.EvalSigma-Pipeline-Func@L63808`, `rule.16.EvalSigma-Pipeline-Closure@L63825`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L63842`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L63858`, `def.16.ClosureLoweringCaptureTypes@L63876`, `rule.16.Layout-ClosureEnv@L63892`, `rule.16.Layout-ClosureEnv-Empty@L63908`, `rule.16.Lower-Expr-Closure-NonCapturing@L63924`
- `rule.16.Lower-Expr-Closure-Capturing@L63940`, `def.16.LowerCaptureEnv@L63958`, `def.16.CapturedIdentifierLoweringHelpers@L63978`, `rule.16.Lower-CapturedIdent-Ref@L63993`, `req.16.LowerCapturedIdentRefTemporaries@L64010`, `rule.16.Lower-CapturedIdent-Move@L64023`, `def.16.ClosureEnvParam@L64040`, `def.16.ClosureCodeSig@L64053`
- `rule.16.Lower-Closure-Call@L64070`, `req.16.LowerClosureCallResolvedInternalForm@L64088`, `rule.16.Lower-Expr-Pipeline@L64101`, `def.16.LowerPipelineCallablePredicates@L64119`, `diag.16.ClosureAndPipelineExpressions@L64137`, `diag.16.ExpressionDiagnosticsSupplement@L64152`

#### `spec.patterns`

Count: 161 total; 161 required; 0 recommended; 0 informative. Ledger line span: L64193-L66741.

- `grammar.17.BasicPatterns@L64193`, `rule.17.Parse-Pattern-Literal@L64210`, `rule.17.Parse-Pattern-Wildcard@L64226`, `rule.17.Parse-Pattern-Identifier@L64242`, `def.17.PatternAstForms@L64260`, `def.17.PatternJudgements@L64276`, `def.17.PermWrap@L64289`, `rule.17.Pat-StripPerm@L64304`
- `def.17.PatternNameExtractionJudgement@L64320`, `rule.17.Pat-Ident-Names@L64333`, `rule.17.Pat-Wild@L64347`, `rule.17.Pat-Lit@L64362`, `rule.17.Pat-Dup-R-Err@L64377`, `rule.17.Pat-Wildcard-R@L64395`, `rule.17.Pat-Ident-R@L64410`, `rule.17.Pat-Literal-R@L64425`
- `def.17.PatternBindingEnvironment@L64443`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64458`, `rule.17.Match-Wildcard@L64480`, `rule.17.Match-Ident@L64495`, `rule.17.Match-Literal@L64510`, `req.17.BasicPatternLoweringShared@L64528`, `diag.17.BasicPatterns@L64543`, `grammar.17.TupleRecordPatterns@L64560`
- `req.17.TuplePatternSingleElementSemicolon@L64579`, `rule.17.Parse-Pattern-Tuple@L64594`, `rule.17.Parse-Pattern-Record@L64610`, `rule.17.Parse-TuplePatternElems-Empty@L64626`, `rule.17.Parse-TuplePatternElems-Single@L64642`, `rule.17.Parse-TuplePatternElems-Many@L64658`, `rule.17.Parse-FieldPatternList-Empty@L64674`, `rule.17.Parse-FieldPatternList-Cons@L64690`
- `rule.17.Parse-FieldPattern@L64706`, `rule.17.Parse-FieldPatternTailOpt-None@L64722`, `rule.17.Parse-FieldPatternTailOpt-Yes@L64738`, `rule.17.Parse-FieldPatternTail-End@L64754`, `rule.17.Parse-FieldPatternTail-TrailingComma@L64770`, `rule.17.Parse-FieldPatternTail-Comma@L64786`, `def.17.FieldPatternAstAndAccessors@L64804`, `rule.17.PatNames-TuplePattern@L64821`
- `rule.17.Pat-Record-Field-Explicit@L64836`, `rule.17.Pat-Record-Field-Implicit@L64852`, `rule.17.PatNames-RecordPattern@L64867`, `rule.17.Pat-Tuple-R-Arity-Err@L64884`, `rule.17.Pat-Tuple-R@L64900`, `rule.17.Pat-Record-R@L64916`, `rule.17.RecordPattern-UnknownField@L64932`, `def.17.MatchRecordJudgement@L64950`
- `rule.17.MatchRecord-Empty@L64964`, `rule.17.MatchRecord-Cons-Implicit@L64979`, `rule.17.MatchRecord-Cons-Explicit@L64995`, `rule.17.Match-Tuple@L65011`, `rule.17.Match-Record@L65027`, `req.17.TupleRecordPatternLoweringShared@L65045`, `diag.17.TupleRecordPatterns@L65060`, `grammar.17.EnumModalPatterns@L65077`
- `req.17.EnumPayloadSingleElementTuple@L65095`, `rule.17.Parse-Pattern-Enum@L65110`, `rule.17.Parse-Pattern-Modal@L65126`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65142`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65158`, `rule.17.Parse-EnumPayloadPatternElems-One@L65174`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65190`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65206`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65222`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65238`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65254`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65270`, `def.17.EnumModalPayloadPatterns@L65288`, `rule.17.Pat-Enum-None@L65302`, `rule.17.Pat-Enum-Tuple@L65317`, `rule.17.Pat-Enum-Record@L65333`
- `rule.17.Pat-Modal-None@L65349`, `rule.17.Pat-Modal-Record@L65364`, `rule.17.Pat-Enum-Unit-R@L65382`, `rule.17.Pat-Enum-Tuple-R@L65398`, `rule.17.Pat-Enum-Record-R@L65414`, `rule.17.Pat-Modal-R@L65430`, `rule.17.Pat-Modal-State-R@L65446`, `def.17.MatchModalJudgement@L65464`
- `rule.17.Match-Modal-Empty@L65477`, `rule.17.Match-Modal-Record@L65492`, `rule.17.Match-Enum-Unit@L65508`, `rule.17.Match-Enum-Tuple@L65524`, `rule.17.Match-Enum-Record@L65540`, `rule.17.Match-Modal-General@L65556`, `rule.17.Match-Modal-State@L65572`, `req.17.EnumModalPatternLoweringShared@L65590`
- `diag.17.EnumModalPatterns@L65605`, `grammar.17.RangePatterns@L65622`, `rule.17.Parse-Pattern@L65639`, `rule.17.Parse-Pattern-Err@L65655`, `rule.17.Parse-Pattern-Range-None@L65671`, `rule.17.Parse-Pattern-Range@L65687`, `def.17.RangePatternAst@L65705`, `rule.17.Pat-Range-R@L65721`
- `rule.17.RangePattern-NonConst@L65737`, `rule.17.RangePattern-Empty@L65753`, `def.17.ConstPat@L65771`, `rule.17.Match-Range-Inclusive@L65784`, `rule.17.Match-Range-Exclusive@L65800`, `req.17.RangePatternLoweringShared@L65817`, `diag.17.RangePatterns@L65832`, `grammar.17.CaseClauses@L65849`
- `def.17.CaseClauseParsingGroup@L65867`, `rule.17.Parse-IfCases-Cons@L65880`, `rule.17.Parse-IfCase@L65896`, `rule.17.Parse-IfCasesTail-End@L65912`, `rule.17.Parse-IfCasesTail-Else@L65928`, `rule.17.Parse-IfCasesTail-Cons@L65944`, `def.17.IfCaseAst@L65962`, `def.17.BindOrder@L65975`
- `req.17.CaseBodyTypingScope@L65990`, `def.17.IfCaseEvaluationJudgements@L66005`, `rule.17.EvalIfCase-Fail@L66019`, `rule.17.EvalIfCase-Hit@L66035`, `rule.17.EvalIfCases-Head@L66051`, `rule.17.EvalIfCases-Tail@L66067`, `rule.17.EvalIfCases-Else@L66083`, `rule.17.EvalIfCases-None@L66099`
- `def.17.PatternLoweringJudgements@L66117`, `rule.17.Lower-Pat-Correctness@L66131`, `def.17.IfCaseValueCorrect@L66147`, `rule.17.Lower-IfCases-Correctness@L66160`, `def.17.PatternTagHelpers@L66176`, `rule.17.TagOf-Enum@L66192`, `rule.17.TagOf-Modal@L66208`, `rule.17.Lower-BindList-Empty@L66224`
- `rule.17.Lower-BindList-Cons@L66239`, `rule.17.Lower-Pat-General@L66255`, `rule.17.Lower-Pat-Err@L66271`, `rule.17.Lower-IfCases@L66287`, `diag.17.CaseClauses@L66305`, `req.17.ExhaustivenessNoSyntax@L66322`, `req.17.ExhaustivenessNotParserOwned@L66337`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66352`
- `def.17.EnumCaseCoverageHelpers@L66374`, `def.17.ModalCaseCoverageHelpers@L66388`, `def.17.UnionCaseCoverageHelpers@L66401`, `def.17.EnumCaseAnalysisGroup@L66418`, `rule.17.T-IfCase-Enum@L66431`, `def.17.ModalCaseAnalysisGroup@L66447`, `rule.17.T-IfCase-Modal@L66460`, `rule.17.IfCase-Modal-NonExhaustive@L66476`
- `def.17.UnionCaseAnalysisGroup@L66492`, `rule.17.T-IfCase-Union@L66505`, `rule.17.IfCase-Union-NonExhaustive@L66521`, `rule.17.Chk-IfCase-Union@L66537`, `def.17.OtherCaseAnalysisGroup@L66553`, `rule.17.T-IfCase-Other@L66566`, `rule.17.Chk-IfCase-Enum@L66582`, `rule.17.IfCase-Enum-NonExhaustive@L66598`
- `rule.17.Chk-IfCase-Modal@L66614`, `rule.17.Chk-IfCase-Other@L66630`, `rule.17.Chk-IfIs@L66646`, `rule.17.Chk-IfIs-No-Else@L66662`, `rule.17.IfCase-Unreachable@L66678`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L66696`, `req.17.ExhaustivenessNoAdditionalLowering@L66711`, `diag.17.ExhaustivenessAndReachability@L66726`
- `diag.17.PatternDiagnosticsSupplement@L66741`

#### `spec.statements`

Count: 260 total; 260 required; 0 recommended; 0 informative. Ledger line span: L66771-L70869.

- `grammar.18.Blocks@L66771`, `req.18.BlockStatementExternalDefinitions@L66801`, `def.18.StatementTerminators@L66817`, `def.18.AttachStmtAttrs@L66832`, `rule.18.Parse-Statement@L66845`, `rule.18.Parse-Statement-Err@L66861`, `rule.18.Parse-Block@L66877`, `def.18.RequiredStatementTerminators@L66893`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L66906`, `rule.18.ConsumeTerminatorOpt-Req-No@L66922`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L66938`, `rule.18.ConsumeTerminatorOpt-Opt-No@L66954`, `def.18.SkipNL@L66970`, `rule.18.ParseStmtSeq-End@L66984`, `rule.18.ParseStmtSeq-TailExpr@L67000`, `rule.18.ParseStmtSeq-Cons@L67016`
- `def.18.SyncStmt@L67032`, `def.18.StatementAstForms@L67047`, `def.18.LastStmtAndResultType@L67060`, `def.18.BindingEnvironmentHelpers@L67079`, `def.18.StatementTypingJudgements@L67096`, `def.18.LoopFlag@L67109`, `def.18.ScopeStackTypeHelpers@L67122`, `rule.18.T-ErrorStmt@L67136`
- `rule.18.BlockInfo-Res@L67151`, `rule.18.BlockInfo-Res-Err@L67167`, `rule.18.BlockInfo-Tail@L67183`, `rule.18.BlockInfo-ReturnTail@L67199`, `rule.18.BlockInfo-Unit@L67215`, `rule.18.T-Block@L67231`, `req.18.BlockCheckingModeValidation@L67247`, `req.18.BlockExprExpressionFormOwnership@L67260`
- `def.18.StatementExecutionJudgements@L67275`, `def.18.ControlAndStatementOutcomes@L67288`, `def.18.BlockExitOutcome@L67307`, `def.18.BlockExit@L67323`, `def.18.EvalBlockBodySigma@L67336`, `def.18.EvalBlockSigma@L67354`, `def.18.EvalBlockBindSigma@L67367`, `def.18.EvalInScopeSigma@L67380`
- `def.18.PlaceEvaluationHelpersGroup@L67393`, `def.18.PlaceJudgements@L67406`, `rule.18.ExecSeq-Empty@L67420`, `rule.18.ExecSeq-Cons-Ok@L67435`, `rule.18.ExecSeq-Cons-Ctrl@L67451`, `rule.18.ExecSigma-Error@L67467`, `def.18.ExecState@L67482`, `rule.18.Step-Exec-Other-Ok@L67495`
- `rule.18.Step-Exec-Other-Ctrl@L67511`, `rule.18.Step-ExecSeq-Ok@L67527`, `rule.18.Step-ExecSeq-Ctrl@L67543`, `rule.18.Step-Exec-Defer@L67559`, `req.18.BlockExprEvalDelegatesToBlock@L67575`, `def.18.LowerStatementJudgements@L67590`, `rule.18.Lower-Stmt-Correctness@L67603`, `rule.18.Lower-Block-Correctness@L67619`
- `def.18.StatementLoweringTotality@L67635`, `rule.18.Lower-StmtList-Empty@L67649`, `rule.18.Lower-StmtList-Cons@L67664`, `rule.18.Lower-Block-Tail@L67680`, `rule.18.Lower-Block-Unit@L67696`, `rule.18.Lower-Stmt-Error@L67712`, `req.18.TemporaryCleanupLowering@L67727`, `def.18.BlockLoopLoweringTotality@L67759`
- `rule.18.Lower-Loop-Infinite@L67775`, `rule.18.Lower-Loop-Cond@L67791`, `rule.18.Lower-Loop-Iter@L67807`, `diag.18.Blocks@L67825`, `grammar.18.BindingStatements@L67842`, `rule.18.Parse-Binding-Stmt@L67860`, `rule.18.Parse-BindingAfterLetVar@L67876`, `rule.18.LetOrVarStmt-Let@L67892`
- `rule.18.LetOrVarStmt-Var@L67908`, `def.18.LetOrVarStmtAst@L67926`, `def.18.BindingAstAndAccessors@L67939`, `def.18.IntroEnt@L67962`, `rule.18.IntroAll-Empty@L67975`, `rule.18.IntroAll-Cons@L67990`, `rule.18.IntroAllVar-Empty@L68006`, `rule.18.IntroAllVar-Cons@L68021`
- `rule.18.T-LetStmt-Ann@L68037`, `rule.18.T-LetStmt-Ann-Mismatch@L68053`, `rule.18.T-LetStmt-Infer@L68069`, `rule.18.T-LetStmt-Infer-Err@L68085`, `req.18.VarStmtTypingMirrorsLet@L68101`, `rule.18.Let-Refutable-Pattern-Err@L68114`, `rule.18.B-LetVar-UniqueNonMove-Err@L68130`, `def.18.SuspendUniqueBind@L68146`
- `rule.18.B-LetVar@L68161`, `rule.18.Prov-LetVar-Ordinary@L68177`, `rule.18.Prov-LetVar-Region-Alias@L68193`, `rule.18.Prov-LetVar-Region-Fresh@L68209`, `def.18.BindVal@L68227`, `def.18.BindPatternRuntimeHelpers@L68240`, `rule.18.BindList-Empty@L68254`, `rule.18.BindList-Cons@L68269`
- `def.18.BindPattern@L68285`, `rule.18.ExecSigma-Let@L68298`, `rule.18.ExecSigma-Let-Ctrl@L68314`, `req.18.VarExecutionMirrorsLet@L68330`, `rule.18.Lower-Stmt-Let@L68345`, `rule.18.Lower-Stmt-Var@L68361`, `diag.18.BindingStatements@L68379`, `grammar.18.LocalUsingStatements@L68396`
- `rule.18.Parse-UsingLocal-Stmt@L68413`, `def.18.UsingLocalStmtAst@L68431`, `req.18.UsingLocalUsesUsingAlias@L68446`, `rule.18.T-UsingLocalStmt@L68459`, `rule.18.T-UsingLocalStmt-Err@L68475`, `req.18.UsingLocalAliasIdentity@L68491`, `rule.18.ExecSigma-UsingLocal@L68506`, `req.18.UsingLocalNoRuntimeEffect@L68521`
- `rule.18.Lower-Stmt-UsingLocal@L68536`, `req.18.UsingLocalNoRuntimeIR@L68551`, `diag.18.LocalUsingStatements@L68566`, `grammar.18.AssignmentStatements@L68583`, `rule.18.Parse-Assign-Stmt@L68601`, `rule.18.AssignOrCompound-Assign@L68617`, `rule.18.AssignOrCompound-Compound@L68633`, `def.18.AssignmentAstForms@L68651`
- `def.18.PlaceRoot@L68665`, `rule.18.T-Assign@L68684`, `rule.18.T-CompoundAssign@L68700`, `rule.18.Assign-NotPlace@L68716`, `rule.18.Assign-Immutable-Err@L68732`, `rule.18.Assign-Type-Err@L68748`, `rule.18.Assign-Const-Err@L68764`, `req.18.AssignmentBindingStateRules@L68780`
- `req.18.AssignmentProvenanceRules@L68793`, `req.18.AssignmentProvenanceEscapeFailures@L68806`, `def.18.AssignmentRootBinding@L68821`, `def.18.DropOnAssign@L68836`, `def.18.DropSubvalueJudgement@L68853`, `rule.18.DropSubvalue-Do@L68866`, `rule.18.DropSubvalue-Skip@L68882`, `rule.18.ExecSigma-Assign@L68898`
- `rule.18.ExecSigma-Assign-Ctrl@L68914`, `rule.18.ExecSigma-CompoundAssign@L68930`, `req.18.CompoundAssignControlPropagation@L68946`, `rule.18.Lower-Stmt-Assign@L68961`, `rule.18.Lower-Stmt-CompoundAssign@L68977`, `diag.18.AssignmentStatements@L68995`, `grammar.18.ExpressionStatements@L69012`, `rule.18.Parse-Expr-Stmt@L69029`
- `def.18.ExprStmtAst@L69047`, `rule.18.T-ExprStmt@L69062`, `req.18.ExprStmtStateAndProvenanceRules@L69078`, `rule.18.ExecSigma-ExprStmt@L69093`, `rule.18.Lower-Stmt-Expr@L69111`, `diag.18.ExpressionStatements@L69129`, `grammar.18.Defer@L69146`, `rule.18.Parse-Defer-Stmt@L69163`
- `def.18.DeferStmtAst@L69181`, `rule.18.T-DeferStmt@L69196`, `rule.18.Defer-NonUnit-Err@L69212`, `rule.18.Defer-NonLocal-Err@L69228`, `rule.18.HasNonLocalCtrl-Return@L69244`, `rule.18.HasNonLocalCtrl-Break@L69259`, `rule.18.HasNonLocalCtrl-Continue@L69275`, `req.18.HasNonLocalCtrlPropagation@L69291`
- `def.18.DeferSafe@L69304`, `req.18.DeferStateAndProvenancePreservation@L69317`, `rule.18.ExecSigma-Defer@L69332`, `req.18.DeferCleanupSmallStep@L69348`, `req.18.DeferCleanupBigStep@L69361`, `rule.18.Lower-Stmt-Defer@L69376`, `diag.18.Defer@L69393`, `grammar.18.Region@L69410`
- `rule.18.Parse-Region-Opts-None@L69429`, `rule.18.Parse-Region-Opts-Some@L69445`, `rule.18.Parse-Region-Alias-None@L69461`, `rule.18.Parse-Region-Alias-Some@L69477`, `rule.18.Parse-Region-Stmt@L69493`, `def.18.RegionStmtAst@L69511`, `def.18.RegionTypeAndFreshNameHelpers@L69524`, `def.18.RegionOptsExpr@L69538`
- `def.18.RegionBind@L69554`, `rule.18.T-RegionStmt@L69569`, `req.18.AnonymousRegionSyntheticBinding@L69585`, `req.18.RegionBindingState@L69598`, `req.18.RegionProvenance@L69611`, `def.18.BindRegionAlias@L69626`, `rule.18.ExecSigma-Region@L69640`, `rule.18.ExecSigma-Region-Ctrl@L69656`
- `def.18.RegionRelease@L69672`, `rule.18.Step-Exec-Region-Enter@L69685`, `rule.18.Step-Exec-Region-Enter-Ctrl@L69701`, `rule.18.Step-Exec-Region-Body@L69717`, `rule.18.Step-Exec-Region-Exit-Ok@L69733`, `rule.18.Step-Exec-Region-Exit-Ctrl@L69749`, `rule.18.Lower-Stmt-Region@L69767`, `diag.18.Region@L69785`
- `grammar.18.Frame@L69802`, `rule.18.Parse-Frame-Stmt@L69819`, `rule.18.Parse-Frame-Explicit@L69835`, `def.18.FrameStmtAst@L69853`, `def.18.InnermostActiveRegion@L69866`, `def.18.FrameBind@L69882`, `rule.18.T-FrameStmt-Implicit@L69899`, `rule.18.T-FrameStmt-Explicit@L69915`
- `rule.18.Frame-NoActiveRegion-Err@L69931`, `rule.18.Frame-Target-NotActive-Err@L69947`, `req.18.FrameSyntheticRegionBinding@L69963`, `req.18.FrameBindingState@L69976`, `req.18.FrameProvenance@L69989`, `def.18.FrameTargetResolution@L70004`, `def.18.FrameEnter@L70018`, `rule.18.ExecSigma-Frame-Implicit@L70031`
- `rule.18.ExecSigma-Frame-Explicit@L70047`, `def.18.FrameReset@L70063`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70076`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70092`, `rule.18.Step-Exec-Frame-Body@L70108`, `rule.18.Step-Exec-Frame-Exit-Ok@L70124`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70140`, `rule.18.Lower-Stmt-Frame-Implicit@L70158`
- `rule.18.Lower-Stmt-Frame-Explicit@L70174`, `diag.18.Frame@L70192`, `grammar.18.ControlTransferStatements@L70209`, `rule.18.Parse-Return-Stmt@L70228`, `rule.18.Parse-Break-Stmt@L70244`, `rule.18.Parse-Continue-Stmt@L70260`, `def.18.ControlTransferAstForms@L70278`, `rule.18.T-Return-Value@L70295`
- `rule.18.T-Return-Unit@L70311`, `rule.18.Return-Async-Type-Err@L70327`, `rule.18.Return-Async-Unit-Err@L70343`, `rule.18.Return-Type-Err@L70359`, `rule.18.Return-Unit-Err@L70375`, `rule.18.T-Break-Value@L70391`, `rule.18.T-Break-Unit@L70407`, `rule.18.Break-Outside-Loop@L70423`
- `rule.18.T-Continue@L70439`, `rule.18.Continue-Outside-Loop@L70455`, `req.18.ControlTransferBindingState@L70471`, `req.18.ControlTransferProvenance@L70484`, `rule.18.ExecSigma-Return@L70499`, `rule.18.ExecSigma-Return-Unit@L70515`, `rule.18.ExecSigma-Return-Ctrl@L70530`, `rule.18.ExecSigma-Break@L70546`
- `rule.18.ExecSigma-Break-Unit@L70562`, `rule.18.ExecSigma-Break-Ctrl@L70577`, `rule.18.ExecSigma-Continue@L70593`, `rule.18.Lower-Stmt-Return@L70610`, `rule.18.Lower-Stmt-Return-Unit@L70626`, `rule.18.Lower-Stmt-Break@L70641`, `rule.18.Lower-Stmt-Break-Unit@L70657`, `rule.18.Lower-Stmt-Continue@L70672`
- `req.18.ControlTransferTemporaryCleanupLowering@L70687`, `diag.18.ControlTransferStatements@L70707`, `grammar.18.UnsafeStatements@L70724`, `rule.18.Parse-Unsafe-Block@L70741`, `def.18.UnsafeBlockStmtAst@L70759`, `rule.18.T-UnsafeStmt@L70774`, `req.18.UnsafeStatementStateAndProvenance@L70790`, `diag.18.UnsafeRequiredOperationOwnership@L70803`
- `rule.18.ExecSigma-UnsafeStmt@L70818`, `rule.18.Lower-Stmt-UnsafeBlock@L70836`, `diag.18.UnsafeStatements@L70854`, `diag.18.StatementDiagnosticsSupplement@L70869`

#### `spec.key-system`

Count: 185 total; 175 required; 0 recommended; 0 informative. Ledger line span: L70903-L74064.

- `grammar.19.KeyPaths@L70903`, `parse.19.KeyPathRules@L70925`, `ast.19.KeyPathForms@L70951`, `requirement.19.KeyPathWellFormedness@L70976`, `requirement.19.KeyAnalysisSharedOnly@L70989`, `def.19.RootExtraction@L71004`, `def.19.ObjectIdentity@L71028`, `def.19.KeyPathFormation@L71049`
- `requirement.19.PointerDereferenceKeyAccess@L71065`, `requirement.19.SharedDynamicClassObjects@L71083`, `def.19.DynMethods@L71096`, `rule.19.K-Witness-Shared-WF@L71109`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71125`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71140`, `def.19.SharedDynamicMethodCallKeyPath@L71153`, `def.19.KeyLoweringForms@L71170`
- `rule.19.Lower-KeyPath@L71185`, `rule.19.Lower-KeyAccess-Uncovered@L71201`, `rule.19.Lower-KeyAccess-Covered@L71217`, `diagnostics.19.KeyPaths@L71235`, `grammar.19.KeyAcquisitionBlocks@L71259`, `requirement.19.OrderedKeyBlockModifier@L71279`, `parse.19.KeyBlockRules@L71294`, `ast.19.KeyBlockForms@L71327`
- `def.19.KeyTriple@L71360`, `rule.19.K-Mode-Read@L71386`, `rule.19.K-Mode-Write@L71402`, `requirement.19.RestrictiveContextApplies@L71418`, `def.19.ReadContexts@L71431`, `def.19.WriteContexts@L71453`, `def.19.KeyStateContext@L71476`, `def.19.Covered@L71499`
- `requirement.19.ValidKeyContext@L71514`, `rule.19.K-Acquire-New@L71533`, `rule.19.K-Acquire-Covered@L71549`, `requirement.19.KeyAcquisitionEvaluationOrder@L71565`, `rule.19.K-Block-Acquire@L71580`, `rule.19.K-Read-Block-No-Write@L71596`, `requirement.19.KeyCoarseningInlineMarker@L71614`, `rule.19.K-Coarsen-Inline@L71627`
- `requirement.19.FieldKeyBoundary@L71643`, `requirement.19.ClosureDependencySetConsumption@L71658`, `def.19.SharedCaptures@L71671`, `def.19.LocalClosureKeyPath@L71684`, `rule.19.K-Closure-Escape-Keys@L71701`, `requirement.19.EscapingClosureSharedLifetime@L71717`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L71730`, `requirement.19.KeyBlockCanonicalOrderReferences@L71749`
- `def.19.KeyBlockRuntimeJudgments@L71762`, `def.19.AcquireKeysSigma@L71775`, `def.19.ReleaseKeysSigma@L71792`, `def.19.ModeOf@L71808`, `rule.19.ExecSigma-KeyBlock@L71825`, `rule.19.ExecSigma-KeyBlock-Ctrl@L71841`, `rule.19.Step-Exec-KeyBlock-Enter@L71857`, `rule.19.Step-Exec-KeyBlock-Body@L71873`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L71889`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L71905`, `requirement.19.ScopeExitKeyRelease@L71921`, `requirement.19.LocalClosureInvocationSharedCaptures@L71936`, `requirement.19.EscapingClosureInvocationSharedCaptures@L71954`, `def.19.LowerKeyPathsEmpty@L71974`, `def.19.LowerKeyPathsCons@L71987`, `rule.19.Lower-Stmt-KeyBlock@L72000`
- `requirement.19.KeyScopeBound@L72021`, `requirement.19.KeyEscapeRestrictions@L72036`, `requirement.19.FineGrainedKeyLoopWarning@L72051`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72064`, `diagnostics.19.KeyAcquisitionBlocks@L72077`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72107`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72122`, `def.19.PrefixAndDisjoint@L72137`
- `def.19.KeyPathOrdering@L72152`, `def.19.KeyCompatibility@L72187`, `def.19.IndexEquivalence@L72218`, `requirement.19.IndexEquivalenceConservativeSubset@L72241`, `rule.19.K-Disjoint-Safe@L72254`, `rule.19.K-Prefix-Coverage@L72270`, `def.19.DynamicIndexDisjointness@L72288`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72311`
- `rule.19.K-Dynamic-Index-Conflict@L72324`, `def.19.ReadThenWrite@L72342`, `requirement.19.ReadThenWriteDiagnosticSurface@L72359`, `requirement.19.ReadThenWriteOtherWriteForms@L72372`, `rule.19.K-Read-Write-Reject@L72385`, `rule.19.K-RMW-Permitted@L72401`, `rule.19.K-RMW-Explicit-Warn@L72417`, `rule.19.K-RMW-Contention-Warn@L72433`
- `def.19.OrderedComparablePaths@L72449`, `rule.19.K-Ordered-Ok@L72465`, `rule.19.K-Ordered-Base-Err@L72481`, `rule.19.K-Ordered-Redundant-Warn@L72497`, `requirement.19.CanonicalOrderDynamicUse@L72515`, `requirement.19.KeyConflictRuntimeCompatibility@L72528`, `def.19.LowerConflictChecks@L72543`, `rule.19.Lower-Key-ConflictChecks@L72560`
- `diagnostics.19.ConflictDetection@L72578`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72603`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L72618`, `ast.19.NestedReleaseForm@L72633`, `rule.19.K-Nested-Same-Path@L72648`, `def.19.SharedParam@L72669`, `def.19.DirectCalleeAccesses@L72683`, `def.19.CalleeAccessSummary@L72696`
- `def.19.CalleeAccessInstantiation@L72709`, `rule.19.K-Reentrant@L72724`, `requirement.19.UnknownCalleeAccessWarning@L72739`, `rule.19.CallSharedArgumentNoKeyAcquisition@L72754`, `requirement.19.StaleOkSuppressesReleaseWarning@L72769`, `rule.19.K-Release-SameMode-Err@L72782`, `requirement.19.NestedReleaseExecutionSequence@L72800`, `rule.19.K-Release-Sequence@L72819`
- `requirement.19.NestedReleaseInterleavingWindow@L72839`, `def.19.HeldKeyAccessors@L72852`, `def.19.ReleasedKeyState@L72868`, `rule.19.ExecSigma-KeyBlock-Release@L72888`, `rule.19.Lower-Stmt-KeyBlock-Release@L72906`, `diagnostics.19.NestedRelease@L72929`, `grammar.19.SpeculativeExecution@L72952`, `parse.19.SpeculativeBlocks@L72969`
- `ast.19.SpeculativeBlockForm@L72984`, `def.19.SpeculativeSetsAndStates@L72997`, `rule.19.K-Spec-Write-Required@L73025`, `rule.19.K-Spec-Pure-Body@L73041`, `requirement.19.SpeculativePermittedOperations@L73057`, `requirement.19.SpeculativeProhibitedOperations@L73075`, `def.19.IsCallLike@L73095`, `rule.19.K-Spec-No-Nested-Key@L73108`
- `rule.19.K-Spec-No-Impure-Call@L73124`, `rule.19.K-Spec-No-Memory-Ordering@L73140`, `rule.19.K-Spec-No-Wait@L73156`, `rule.19.K-Spec-No-Defer@L73172`, `rule.19.K-Spec-No-Release@L73188`, `rule.19.ExecSigma-KeyBlock-Speculative@L73208`, `def.19.SpecLoop@L73224`, `rule.19.Spec-Start@L73245`
- `rule.19.Spec-Snapshot@L73260`, `rule.19.Spec-Exec-Ok@L73276`, `rule.19.Spec-Exec-Panic@L73292`, `rule.19.Spec-Commit-Success@L73308`, `rule.19.Spec-Commit-Fail-Retry@L73324`, `rule.19.Spec-Commit-Fail-Fallback@L73340`, `rule.19.Spec-Retry@L73356`, `rule.19.Spec-Fallback@L73371`
- `rule.19.SpecBlock-Ok@L73387`, `rule.19.SpecBlock-Panic@L73403`, `def.19.SpeculativeRuntimeHelpers@L73419`, `requirement.19.SpeculativePanicDiscardsWrites@L73440`, `requirement.19.SpeculativeAtomicity@L73453`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73466`, `def.19.SpeculativeIR@L73483`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73496`
- `diagnostics.19.SpeculativeExecution@L73516`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73544`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73559`, `def.19.StaticallySafeConditions@L73574`, `requirement.19.StaticallySafeSoundProofRequired@L73596`, `rule.19.K-Static-Safe@L73615`, `requirement.19.NoRuntimeSyncMeaning@L73631`, `rule.19.K-Static-Required@L73646`
- `requirement.19.RuntimeSynchronizationRequirements@L73664`, `requirement.19.DynamicIndexRuntimeOrdering@L73682`, `requirement.19.DynamicIndexedPathCoarsening@L73701`, `requirement.19.CanonicalOrderDeadlockFreedom@L73716`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L73731`, `rule.19.K-Dynamic-Permitted@L73746`, `requirement.19.DynamicContextStaticSafeLowering@L73762`, `diagnostics.19.DynamicKeyVerification@L73777`
- `grammar.19.MemoryOrdering@L73798`, `parse.19.MemoryOrdering@L73818`, `ast.19.MemoryOrderingForms@L73835`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L73858`, `def.19.MemoryOrderingLevels@L73873`, `requirement.19.MemoryOrderAttributeAttachment@L73894`, `requirement.19.ExpressionMemoryOrderWellFormedness@L73912`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L73925`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L73938`, `rule.19.T-Fence@L73951`, `requirement.19.FenceContextAndHeldKeys@L73967`, `requirement.19.FenceEvaluation@L73982`, `requirement.19.FenceOrderingConstraints@L73999`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74016`, `rule.19.Lower-Expr-Fence@L74031`, `rule.19.Lower-Ordered-Access@L74046`
- `diagnostics.19.MemoryOrdering@L74064`

#### `spec.structured-parallelism`

Count: 181 total; 180 required; 0 recommended; 0 informative. Ledger line span: L74083-L77316.

- `grammar.20.ParallelBlocks@L74083`, `parse.20.ParallelBlockRules@L74108`, `ast.20.ParallelBlockForms@L74138`, `def.20.ParallelBlockOptionValidation@L74174`, `rule.20.Dim3Const-Err@L74200`, `def.20.ParallelDomainCtorValidation@L74216`, `rule.20.T-Parallel@L74236`, `requirement.20.ParallelBlockWellFormedness@L74252`
- `rule.20.Parallel-Domain-Param-Err@L74269`, `requirement.20.ParallelCancelOptionType@L74285`, `def.20.ParallelState@L74300`, `def.20.ParallelGpuTopologyOptions@L74319`, `def.20.AwaitSpawned@L74351`, `rule.20.EvalSigma-Parallel@L74364`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74380`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74396`
- `requirement.20.ParallelPanicPropagationReference@L74412`, `def.20.ParallelLoweringJudgments@L74427`, `rule.20.Lower-Expr-Parallel@L74440`, `diagnostics.20.ParallelBlocks@L74458`, `requirement.20.ExecutionDomainSyntax@L74479`, `grammar.20.ExecutionDomainExamples@L74492`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74511`, `parse.20.GpuPtrGenericType@L74524`
- `def.20.GpuDomainJudgments@L74539`, `def.20.GpuMemoryForms@L74558`, `def.20.GpuPtrType@L74578`, `def.20.DispatchGpuTopologyComputation@L74592`, `def.20.GpuExecutionTopology@L74615`, `def.20.GpuIntrinsicTable@L74638`, `def.20.GpuRuntimeState@L74664`, `def.20.ExecutionDomainClass@L74690`
- `requirement.20.ExecutionDomainContextMethods@L74710`, `def.20.GpuSafeType@L74737`, `def.20.GpuSafePredicateClauses@L74766`, `rule.20.GpuSafe-Prim@L74781`, `rule.20.GpuSafe-RawPtr@L74797`, `rule.20.GpuSafe-Array@L74813`, `rule.20.GpuSafe-Tuple@L74829`, `rule.20.GpuSafe-Perm@L74845`
- `rule.20.GpuSafe-Record@L74861`, `rule.20.GpuSafe-Enum@L74877`, `rule.20.GpuSafe-StringView@L74893`, `rule.20.GpuSafe-BytesView@L74909`, `rule.20.GpuSafeType-Err@L74925`, `rule.20.GpuSafe-Record-Field-Err@L74941`, `rule.20.GpuSafe-Generic-Unbounded-Err@L74957`, `rule.20.T-GpuIntrinsic@L74973`
- `rule.20.Barrier-Outside-Err@L74989`, `rule.20.GpuIntrinsic-Outside-Err@L75005`, `rule.20.GpuPtr-AddrSpace-Err@L75021`, `requirement.20.ExecutionDomainDispatchableClass@L75037`, `requirement.20.GpuSafeGenericBounds@L75050`, `requirement.20.KeySystemUnavailableInGpuContext@L75063`, `requirement.20.InlineDomainSemantics@L75078`, `def.20.GpuMemoryVisibility@L75096`
- `rule.20.GpuPtr-Deref-Visible@L75112`, `rule.20.GpuPtr-Deref-Err@L75128`, `def.20.GpuTopologyValidity@L75144`, `rule.20.EvalSigma-GPU-Parallel@L75163`, `rule.20.EvalSigma-GPU-Dispatch@L75179`, `rule.20.GpuExecute-Step@L75195`, `rule.20.GpuBarrier-Sync@L75211`, `requirement.20.GpuBarrierWait@L75227`
- `rule.20.EvalSigma-GpuBarrier@L75242`, `rule.20.Barrier-Divergence-Err@L75258`, `rule.20.KeyBlock-GPU-Err@L75274`, `rule.20.WorkgroupSize-Err@L75290`, `rule.20.Lower-Domain-CPU@L75308`, `rule.20.Lower-Domain-GPU@L75323`, `rule.20.Lower-Domain-Inline@L75338`, `rule.20.Lower-Expr-Parallel-GPU@L75353`
- `rule.20.Lower-Expr-GpuBarrier@L75369`, `diagnostics.20.ExecutionDomains@L75386`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75414`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75429`, `requirement.20.CaptureSetComputationReference@L75444`, `def.20.GpuCaptureJudgments@L75466`, `requirement.20.ParallelCapturePermissions@L75483`, `rule.20.Parallel-Closure-Capture-Const@L75500`
- `rule.20.Parallel-Closure-Capture-Shared@L75516`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75532`, `def.20.OuterParallelMoveSelection@L75548`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75562`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75578`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75594`, `requirement.20.ParallelClosuresLocalForKeys@L75610`, `rule.20.GpuCaptureOk-Const@L75623`
- `rule.20.GpuCaptureOk-Unique-Move@L75639`, `rule.20.GpuCapture-Shared-Err@L75655`, `rule.20.GpuCapture-HeapProv-Err@L75671`, `rule.20.GpuCapture-NonGpuSafe-Err@L75687`, `requirement.20.MovedBindingValidityReference@L75703`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L75718`, `requirement.20.CaptureSemanticsGenericLowering@L75737`, `diagnostics.20.CaptureSemantics@L75752`
- `grammar.20.Spawn@L75776`, `parse.20.SpawnRules@L75797`, `ast.20.SpawnForms@L75824`, `def.20.SpawnOptionValidation@L75857`, `requirement.20.SpawnRequiresParallelContext@L75876`, `rule.20.T-Spawn@L75889`, `def.20.SpawnHandleAndEnqueue@L75907`, `requirement.20.SpawnEvaluationProcedure@L75924`
- `rule.20.EvalSigma-Spawn@L75944`, `requirement.20.SpawnedResultRetrievalReference@L75960`, `rule.20.Lower-Expr-Spawn@L75975`, `diagnostics.20.Spawn@L75993`, `grammar.20.Dispatch@L76012`, `parse.20.DispatchRules@L76037`, `ast.20.DispatchForms@L76071`, `requirement.20.DispatchRequiresParallelContext@L76114`
- `rule.20.T-Dispatch@L76127`, `rule.20.T-Dispatch-Reduce@L76143`, `rule.20.T-GPU-Dispatch@L76159`, `rule.20.T-GPU-Dispatch-Reduce@L76175`, `def.20.DispatchAccessInference@L76191`, `def.20.DispatchOptionsAndDynamicKeys@L76229`, `rule.20.Dispatch-Infer-Err@L76260`, `rule.20.Dispatch-Outside-Err@L76276`
- `rule.20.Dispatch-Chunk-Type-Err@L76292`, `rule.20.Dispatch-Dependency-Err@L76308`, `rule.20.Dispatch-Reduce-Assoc-Err@L76324`, `rule.20.Dispatch-DynamicKey-Warn@L76340`, `requirement.20.DispatchKeyInferenceRequired@L76356`, `rule.20.DispatchIndexedDisjointness@L76369`, `requirement.20.DispatchReductionAssociativity@L76384`, `requirement.20.DispatchChunkSemanticsStatic@L76397`
- `def.20.DispatchPartitionSpec@L76412`, `def.20.DispatchIndexAndPathDisjointness@L76427`, `def.20.DispatchPartitioning@L76470`, `def.20.DispatchReductionAndChunking@L76491`, `rule.20.EvalSigma-Dispatch@L76513`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76529`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76545`, `def.20.DispatchRun@L76561`
- `rule.20.Lower-Expr-Dispatch@L76582`, `diagnostics.20.Dispatch@L76600`, `requirement.20.CancellationSyntax@L76623`, `requirement.20.CancellationNoAdditionalParsingRules@L76638`, `ast.20.CancelTokenForms@L76653`, `requirement.20.CancelTokenStaticSemantics@L76680`, `requirement.20.CancelTokenParallelAvailability@L76705`, `def.20.CancelRuntimeHelpers@L76718`
- `rule.20.Cancel-New@L76741`, `rule.20.Cancel-Child@L76757`, `rule.20.Cancel-IsCancelled@L76773`, `rule.20.Cancel-DoCancel@L76789`, `rule.20.Cancel-WaitCancelled-Completed@L76805`, `rule.20.Cancel-WaitCancelled-Suspended@L76821`, `requirement.20.CooperativeCancellationBehavior@L76837`, `def.20.CancelIR@L76859`
- `rule.20.Lower-Cancel-New@L76872`, `rule.20.Lower-Cancel-Request@L76887`, `rule.20.Lower-Cancel-Wait@L76902`, `requirement.20.CancellationCheckpointLowering@L76917`, `requirement.20.SpawnDispatchCancellationLowering@L76930`, `diagnostics.20.Cancellation@L76945`, `requirement.20.PanicHandlingNoAdditionalSyntax@L76962`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L76977`
- `ast.20.ParallelPanicPropagationInputs@L76992`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77007`, `requirement.20.ParallelWorkItemPanicSemantics@L77022`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77039`, `requirement.20.ParallelPanicCancellationRequest@L77055`, `def.20.FirstCompletedFailure@L77068`, `rule.20.Lower-Parallel-Join-Panic@L77083`, `diagnostics.20.PanicHandling@L77100`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77117`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77132`, `ast.20.DeterminismNestingForms@L77147`, `requirement.20.DispatchDeterminismConditions@L77162`, `requirement.20.OrderedDispatchSequentialSideEffects@L77179`, `requirement.20.NoNestedGpuParallel@L77192`, `requirement.20.NestedParallelRuntimeSemantics@L77207`, `def.20.ParallelDeterministicOrdering@L77228`
- `rule.20.Lower-Deterministic-Dispatch@L77254`, `rule.20.Lower-Nested-Parallel@L77270`, `diagnostics.20.DeterminismAndNesting@L77286`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77303`, `diagnostics.20.StructuredParallelismSupplement@L77316`

#### `spec.async`

Count: 254 total; 253 required; 0 recommended; 0 informative. Ledger line span: L77337-L82007.

- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77337`, `requirement.21.ReservedAsyncTypeConstructors@L77350`, `requirement.21.AsyncParameterDefaults@L77370`, `requirement.21.ReservedAsyncStates@L77383`, `parse.21.AsyncTypes@L77402`, `parse.21.UnappliedAsyncPath@L77417`, `ast.21.AsyncModalDeclaration@L77432`, `ast.21.AsyncAliases@L77503`
- `ast.21.AsyncCombinatorMembers@L77524`, `def.21.AsyncSigAndBodyReturnType@L77544`, `rule.21.Sub-Async@L77571`, `rule.21.WF-Async@L77592`, `rule.21.WF-Async-ArgCount-Err@L77610`, `rule.21.WF-Async-Arg-WF-Err@L77628`, `rule.21.WF-Async-Path-Err@L77646`, `requirement.21.AsyncFailedUninhabitedForNeverError@L77664`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L77679`, `def.21.AsyncTypeLoweringForms@L77696`, `requirement.21.AsyncNeverErrorLowering@L77727`, `rule.21.Lower-Async-Type@L77740`, `rule.21.Lower-Async-Alias@L77760`, `diagnostics.21.AsyncType@L77780`, `grammar.21.SuspensionForms@L77799`, `parse.21.SuspensionFormsPrimaryExpressions@L77818`
- `rule.21.Parse-Wait-Expr@L77833`, `rule.21.Parse-Yield-From-Expr@L77853`, `rule.21.Parse-Yield-Expr@L77876`, `ast.21.SuspensionForms@L77899`, `ast.21.SuspensionFormResolution@L77919`, `ast.21.SuspensionFormEvaluationOrder@L77938`, `rule.21.T-Wait@L77961`, `rule.21.T-Wait-Future@L77979`
- `rule.21.Wait-Handle-Err@L77997`, `rule.21.T-Yield@L78017`, `rule.21.Yield-NotAsync-Err@L78035`, `rule.21.Yield-Out-Err@L78053`, `rule.21.T-Yield-From@L78073`, `rule.21.YieldFrom-NotAsync-Err@L78091`, `rule.21.YieldFrom-Out-Err@L78109`, `rule.21.YieldFrom-In-Err@L78128`
- `rule.21.YieldFrom-ErrType-Err@L78146`, `requirement.21.SuspensionKeyRestrictionsReference@L78164`, `requirement.21.WaitRuntimeSemantics@L78179`, `def.21.WaitRuntimeHelpers@L78201`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78225`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78243`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78262`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78275`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78293`, `rule.21.EvalSigma-Wait-Ctrl@L78312`, `requirement.21.YieldRuntimeSemantics@L78330`, `def.21.ResumptionHelpers@L78350`, `rule.21.EvalSigma-Yield@L78385`, `rule.21.EvalSigma-Yield-Release@L78404`, `rule.21.EvalSigma-Yield-Resume@L78423`, `requirement.21.YieldFromRuntimeSemantics@L78442`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78462`, `rule.21.EvalSigma-YieldFrom-Completed@L78482`, `rule.21.EvalSigma-YieldFrom-Failed@L78500`, `rule.21.EvalSigma-YieldFrom-Resume@L78518`, `def.21.EvalYieldFromContinueSignature@L78537`, `rule.21.EvalYieldFromContinue-Suspended@L78552`, `rule.21.EvalYieldFromContinue-Completed@L78572`, `rule.21.EvalYieldFromContinue-Failed@L78590`
- `def.21.SuspensionLoweringForms@L78610`, `rule.21.Lower-Wait-Spawned@L78631`, `rule.21.Lower-Wait-Tracked@L78649`, `rule.21.Lower-Yield@L78667`, `rule.21.Lower-Yield-Release@L78685`, `requirement.21.YieldReleaseReacquireLowering@L78703`, `rule.21.Lower-YieldFrom@L78716`, `requirement.21.YieldFromEnterLoweringLoop@L78734`
- `diagnostics.21.SuspensionForms@L78752`, `requirement.21.AsyncIterationSyntax@L78777`, `grammar.21.CompositionForms@L78792`, `requirement.21.AsyncMethodCallSurfaces@L78811`, `requirement.21.UntilMethodCallSurface@L78831`, `parse.21.CompositionPrimaryExpressions@L78846`, `rule.21.Parse-Sync-Expr@L78861`, `rule.21.Parse-Race-Expr@L78881`
- `rule.21.Parse-RaceArms-Cons@L78901`, `rule.21.Parse-RaceArm@L78919`, `rule.21.Parse-RaceArmsTail-End@L78939`, `rule.21.Parse-RaceArmsTail-TrailingComma@L78957`, `rule.21.Parse-RaceArmsTail-Comma@L78975`, `rule.21.Parse-RaceHandler-Yield@L78993`, `rule.21.Parse-RaceHandler-Return@L79011`, `rule.21.Parse-All-Expr@L79031`
- `parse.21.CompositionOrdinarySurfaces@L79049`, `ast.21.CompositionForms@L79064`, `ast.21.AsyncIterationLoopForm@L79086`, `ast.21.CompositionMethodCallForms@L79103`, `ast.21.CompositionResolution@L79118`, `ast.21.CompositionEvaluationOrder@L79147`, `rule.21.T-Loop-Iter-Async@L79173`, `rule.21.Loop-Async-Err@L79195`
- `requirement.21.ManualSteppingRequirement@L79213`, `def.21.SyncYieldContainment@L79228`, `rule.21.Sync-Yield-Err@L79244`, `rule.21.Sync-YieldFrom-Err@L79262`, `rule.21.T-Sync@L79280`, `rule.21.Sync-Async-Context-Err@L79298`, `rule.21.Sync-Out-Err@L79316`, `rule.21.Sync-In-Err@L79335`
- `def.21.RaceMode@L79355`, `rule.21.T-Race@L79373`, `rule.21.T-Race-Stream@L79394`, `rule.21.Race-Arity-Err@L79415`, `rule.21.Race-Handler-Mix-Err@L79433`, `rule.21.Race-Operand-Out-Err@L79451`, `rule.21.Race-Operand-Err@L79470`, `rule.21.Race-Stream-Operand-Err@L79489`
- `rule.21.Race-Handler-Type-Err@L79508`, `rule.21.Race-Stream-Handler-Type-Err@L79529`, `rule.21.T-All@L79552`, `rule.21.All-Out-Err@L79571`, `rule.21.All-In-Err@L79589`, `def.21.UntilType@L79607`, `def.21.AsyncCombinatorTypes@L79624`, `requirement.21.AsyncCombinatorMemberLookup@L79645`
- `rule.21.T-Async-Map@L79660`, `rule.21.T-Async-Filter@L79678`, `rule.21.T-Async-Take@L79696`, `rule.21.T-Async-Fold@L79714`, `rule.21.T-Async-Chain@L79732`, `requirement.21.AsyncIterationRuntimeSemantics@L79752`, `requirement.21.ManualSteppingRuntimeSemantics@L79770`, `requirement.21.SyncRuntimeSemantics@L79783`
- `def.21.SyncStepSignature@L79803`, `rule.21.SyncStep-Suspended@L79818`, `rule.21.EvalSigma-Sync-Suspended@L79836`, `rule.21.EvalSigma-Sync-Completed@L79855`, `rule.21.EvalSigma-Sync-Failed@L79873`, `requirement.21.RaceReturnRuntimeSemantics@L79891`, `requirement.21.RaceStreamingRuntimeSemantics@L79909`, `def.21.RaceSelectionAndState@L79930`
- `rule.21.InitRace@L79960`, `def.21.RaceStepReturnSignature@L79979`, `rule.21.RaceStepReturn-Completed@L79994`, `rule.21.RaceStepReturn-Failed@L80014`, `rule.21.RaceStepReturn-Continue@L80033`, `rule.21.EvalSigma-Race-Return@L80053`, `def.21.RaceStepStreamSignature@L80072`, `rule.21.RaceStepStream-Yield-Initial@L80087`
- `rule.21.RaceStepStream-AllComplete@L80107`, `rule.21.RaceStepStream-Failed@L80125`, `rule.21.EvalSigma-Race-Stream@L80144`, `def.21.CancelAllSignature@L80163`, `rule.21.CancelAll@L80178`, `def.21.RaceStreamSuspensionState@L80198`, `rule.21.RaceStepStream-Yield-Resumable@L80218`, `rule.21.ResumeRaceState-Step@L80238`
- `rule.21.ResumeRaceState-Done@L80257`, `rule.21.EvalSigma-Race-Stream-Resume@L80274`, `requirement.21.StreamingRaceResumptionOrder@L80294`, `requirement.21.AllRuntimeSemantics@L80307`, `def.21.AllStateAndInitSignature@L80327`, `rule.21.InitAll@L80343`, `def.21.AllStepSignature@L80362`, `rule.21.AllStep-Complete@L80377`
- `rule.21.AllStep-Failed@L80396`, `rule.21.AllStep-Resume@L80416`, `def.21.AllLoopSignature@L80436`, `rule.21.AllLoop-AllCompleted@L80451`, `rule.21.AllLoop-Failed@L80469`, `rule.21.AllLoop-Continue@L80487`, `rule.21.EvalSigma-All@L80506`, `requirement.21.UntilRuntimeSemantics@L80524`
- `def.21.AsyncCombinatorRuntimeWrappers@L80541`, `rule.21.EvalSigma-Map-Create@L80564`, `rule.21.EvalSigma-Map-Resume-Yield@L80582`, `rule.21.EvalSigma-Map-Resume-Complete@L80600`, `rule.21.EvalSigma-Map-Resume-Failed@L80618`, `rule.21.EvalSigma-Filter-Create@L80636`, `rule.21.EvalSigma-Filter-Resume-Pass@L80654`, `rule.21.EvalSigma-Filter-Resume-Skip@L80672`
- `rule.21.EvalSigma-Filter-Resume-Complete@L80691`, `rule.21.EvalSigma-Take-Create@L80709`, `rule.21.EvalSigma-Take-Resume-Yield@L80727`, `rule.21.EvalSigma-Take-Resume-Done@L80745`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L80763`, `rule.21.EvalSigma-Fold-Create@L80781`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L80799`, `rule.21.EvalSigma-Fold-Resume-Complete@L80817`
- `rule.21.EvalSigma-Fold-Resume-Failed@L80835`, `rule.21.EvalSigma-Chain-Create@L80853`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L80871`, `rule.21.EvalSigma-Chain-Resume-Chained@L80889`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L80907`, `def.21.AsyncComposeIR@L80927`, `rule.21.Lower-Expr-Sync@L80940`, `requirement.21.SyncLoopIRSemantics@L80958`
- `rule.21.Lower-Expr-Race-Return@L80971`, `rule.21.Lower-Expr-Race-Stream@L80989`, `requirement.21.RaceInitIRSemantics@L81007`, `requirement.21.RaceResumeIRSemantics@L81023`, `rule.21.Lower-Expr-All@L81036`, `requirement.21.AllJoinIRSemantics@L81052`, `requirement.21.AsyncCombinatorWrapperLowering@L81065`, `rule.21.Lower-Async-Map@L81078`
- `rule.21.Lower-Async-Filter@L81094`, `rule.21.Lower-Async-Take@L81110`, `rule.21.Lower-Async-Fold@L81126`, `rule.21.Lower-Async-Chain@L81142`, `requirement.21.AsyncWrapperLoweringSemantics@L81158`, `diagnostics.21.AsyncCompositionDiagnostics@L81173`, `requirement.21.AsyncStateMachineSyntaxSurface@L81203`, `def.21.AsyncProcedureDefinition@L81216`
- `requirement.21.AsyncStateMachineParsingSurface@L81231`, `def.21.AsyncStateMachineHelperForms@L81248`, `requirement.21.AsyncFrameStoredState@L81277`, `def.21.LiveAcrossSuspension@L81296`, `rule.21.Warn-Async-LargeCapture@L81311`, `rule.21.Warn-Async-LargeCapture-Ok@L81329`, `requirement.21.AsyncLargeCaptureWarningEmission@L81347`, `rule.21.Async-Capture-Err@L81360`
- `rule.21.P-Async-Create@L81378`, `rule.21.Prov-Async-Escape-Err@L81397`, `requirement.21.AsyncErrorPropagationTypingReference@L81416`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81431`, `requirement.21.AsyncSettlementRuntimeSemantics@L81449`, `requirement.21.AsyncResumeRuntimeSemantics@L81466`, `requirement.21.AsyncFailureRuntimeSemantics@L81482`, `def.21.AsyncStateMachineLoweringJudgements@L81502`
- `def.21.AsyncStateMachineFrameHelpers@L81516`, `rule.21.Lower-Async-Proc@L81532`, `requirement.21.AsyncFrameInitIRSemantics@L81552`, `rule.21.Lower-Async-Resume@L81571`, `requirement.21.AsyncResumeSwitchIRSemantics@L81590`, `rule.21.Lower-Async-Suspend@L81604`, `rule.21.Lower-Async-Complete@L81623`, `rule.21.Lower-Async-Fail@L81642`
- `requirement.21.AsyncFailStateIRSemantics@L81661`, `diagnostics.21.AsyncStateMachineDiagnostics@L81677`, `requirement.21.AsyncKeySyntaxSurface@L81698`, `requirement.21.AsyncKeyParsingSurface@L81713`, `def.21.AsyncKeyExistingAstForms@L81730`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L81746`, `requirement.21.AsyncKeyRestrictions@L81763`, `rule.21.A-Closure-Yield-Keys-Err@L81780`
- `requirement.21.SharedCapturingClosureYieldKeys@L81799`, `requirement.21.YieldReleaseStalenessWarning@L81812`, `requirements.21.AsyncCapabilityRequirements@L81827`, `requirement.21.AsyncSuspensionAccessRights@L81847`, `requirement.21.YieldReleaseRuntimeReference@L81860`, `requirement.21.AsyncKeyFailureHandlingReference@L81873`, `def.21.AsyncKeyIR@L81888`, `rule.21.Lower-Wait-Key-Illegal@L81901`
- `rule.21.Lower-Yield-Release-Keys@L81919`, `rule.21.Lower-YieldFrom-Release-Keys@L81937`, `rule.21.Lower-Closure-Yield-Shared@L81955`, `requirement.21.StaleValueMarkIRDiagnostics@L81973`, `diagnostics.21.AsyncKeyDiagnostics@L81988`, `diagnostics.21.AsyncDiagnosticsSupplement@L82007`

#### `spec.comptime`

Count: 181 total; 181 required; 0 recommended; 0 informative. Ledger line span: L82027-L85037.

- `requirement.22.Phase2ExecutionPosition@L82027`, `grammar.22.CompileTimeForms@L82044`, `def.22.CtParseJudg@L82066`, `rule.22.Parse-CtProc@L82079`, `rule.22.Parse-CtStmt@L82095`, `rule.22.Parse-CtExpr@L82111`, `rule.22.Parse-CtIf@L82127`, `rule.22.Parse-CtLoopIter@L82143`
- `rule.22.Parse-CtElseOpt-None@L82159`, `rule.22.Parse-CtElseOpt-Block@L82175`, `rule.22.Parse-CtElseOpt-ElseIf@L82191`, `def.22.CtNodeForms@L82209`, `def.22.CtExecutionState@L82228`, `def.22.CompileTimeJudgementSets@L82256`, `def.22.CtValueForms@L82275`, `def.22.CompileTimeTypingEnvironment@L82296`
- `def.22.CtAvailabilityAndForbiddenTypes@L82309`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82342`, `requirement.22.CompileTimeProhibitedConstructs@L82355`, `rule.22.T-CtStmt@L82373`, `rule.22.T-CtExpr@L82389`, `rule.22.T-CtIf@L82405`, `rule.22.T-CtLoopIter@L82421`, `rule.22.T-CtProc@L82437`
- `requirement.22.CompileTimeProcedureContracts@L82453`, `requirement.22.CompileTimeProcedureContextRestriction@L82466`, `requirement.22.ComptimeIfSelectedBranchOnly@L82479`, `requirement.22.ComptimeLoopIterationSemantics@L82492`, `def.22.Phase2ModuleOrder@L82508`, `def.22.CtDynamicHelpers@L82521`, `requirement.22.ComptimePassExecutionRequirements@L82543`, `requirement.22.CtEvalOrdinarySemantics@L82562`
- `requirement.22.CtExpandOrdinaryTraversal@L82575`, `rule.22.ComptimePass-Empty@L82588`, `rule.22.ComptimePass-Cons@L82603`, `rule.22.ComptimePass@L82619`, `rule.22.CtExecModule@L82637`, `rule.22.CtExpandItemSeq-Empty@L82653`, `rule.22.CtExpandItemSeq-Cons@L82668`, `def.22.CtExpandItemResult@L82684`
- `requirement.22.CtPendingEmitsTransfer@L82697`, `rule.22.CtExpandItem-CtProc@L82710`, `rule.22.CtExpandStmtSeq-Empty@L82726`, `rule.22.CtExpandStmtSeq-Cons@L82741`, `rule.22.CtExpandBlock@L82757`, `rule.22.CtExpandStmt-CtStmt@L82773`, `rule.22.CtExpandExpr-CtExpr@L82789`, `rule.22.CtExpandExpr-CtIf-True@L82805`
- `rule.22.CtExpandExpr-CtIf-False@L82821`, `rule.22.CtExpandExpr-CtLoopIter@L82837`, `rule.22.CtLoopIterUnroll-Empty@L82853`, `rule.22.CtLoopIterUnroll-Cons@L82868`, `def.22.CtLiteralize@L82884`, `requirement.22.CompileTimeFormsLowering@L82908`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L82928`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L82945`
- `def.22.CtCapName@L82960`, `rule.22.Parse-CtCapRef@L82973`, `requirement.22.CtCapMethodCallParsing@L82989`, `def.22.CtCapabilitiesAndBuiltinTypes@L83004`, `def.22.CtReflectionInfoFields@L83027`, `def.22.CtValueConversionHelpers@L83043`, `def.22.TypeEmitterInterface@L83069`, `def.22.IntrospectInterface@L83085`
- `def.22.ProjectFilesInterface@L83107`, `def.22.ComptimeDiagnosticsInterface@L83127`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83149`, `requirement.22.TypeEmitterAvailability@L83162`, `requirement.22.ProjectFilesAvailability@L83177`, `def.22.CtCapBindings@L83190`, `requirement.22.ProjectFilesPathRestrictions@L83203`, `requirement.22.TypeEmitterEmitTypeRequirement@L83220`
- `def.22.CtCapabilityDynamicHelpers@L83235`, `rule.22.CtBuiltin-Emit@L83258`, `rule.22.CtBuiltin-ProjectRoot@L83274`, `rule.22.CtBuiltin-Read@L83290`, `rule.22.CtBuiltin-Read-InvalidPath@L83306`, `rule.22.CtBuiltin-ReadBytes@L83322`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83338`, `rule.22.CtBuiltin-Exists@L83354`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83370`, `rule.22.CtBuiltin-ListDir@L83386`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83402`, `rule.22.CtBuiltin-Diagnostics-Error@L83418`, `rule.22.CtBuiltin-Diagnostics-Warning@L83434`, `rule.22.CtBuiltin-Diagnostics-Note@L83450`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83466`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83482`
- `requirement.22.ProjectFileSnapshotStability@L83498`, `requirement.22.CompileTimeCapabilitiesLowering@L83513`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83528`, `grammar.22.TypeLiteral@L83545`, `def.22.ReflectParseJudg@L83562`, `rule.22.Parse-TypeLiteral@L83575`, `def.22.Reflectable@L83593`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L83618`
- `def.22.TypeCategory@L83632`, `def.22.ReflectFields@L83673`, `def.22.ReflectVariants@L83690`, `def.22.ReflectStates@L83707`, `def.22.ReflectionPayloadAndModuleHelpers@L83725`, `rule.22.T-TypeLiteral@L83750`, `requirement.22.IntrospectCategoryValidity@L83766`, `requirement.22.IntrospectMemberValidity@L83779`
- `requirement.22.ReflectionCanonicalOrder@L83794`, `requirement.22.IntrospectImplementsFormSemantics@L83810`, `rule.22.CtEval-TypeLiteral@L83825`, `rule.22.CtBuiltin-Reflect-Category@L83841`, `rule.22.CtBuiltin-Reflect-Fields@L83857`, `rule.22.CtBuiltin-Reflect-Variants@L83873`, `rule.22.CtBuiltin-Reflect-States@L83889`, `rule.22.CtBuiltin-Reflect-Form@L83905`
- `rule.22.CtBuiltin-Reflect-TypeName@L83921`, `rule.22.CtBuiltin-Reflect-ModulePath@L83937`, `requirement.22.ReflectionPurityAndImmutability@L83953`, `requirement.22.ReflectionLowering@L83968`, `diagnostics.22.ReflectionDiagnosticsReference@L83983`, `grammar.22.QuoteSpliceEmission@L84000`, `def.22.QuoteParseJudg@L84022`, `def.22.CaptureQuotedTokens@L84035`
- `rule.22.Parse-Quote-Raw@L84048`, `rule.22.Parse-Quote-Type@L84064`, `rule.22.Parse-Quote-Pattern@L84080`, `def.22.AstForms@L84098`, `def.22.QuoteSpliceHygieneForms@L84117`, `def.22.QuoteJudg@L84134`, `def.22.ExpectedAstKind@L84147`, `def.22.CtLiteralType@L84165`
- `def.22.SpliceCompat@L84186`, `requirement.22.QuoteCompileTimeOnly@L84206`, `def.22.ResolveQuoteKind@L84219`, `requirement.22.QuotedContentValidity@L84234`, `requirement.22.SpliceContextAndTypeCompatibility@L84247`, `requirement.22.SpliceIdentifierPositionRestrictions@L84260`, `requirement.22.StringSpliceIdentifierHygiene@L84273`, `requirement.22.EmitterEmitWellFormedness@L84286`
- `def.22.ParseQuotedBody@L84301`, `def.22.RenderSplice@L84318`, `requirement.22.HygienizeAstProperties@L84336`, `requirement.22.HygienicInternalReferences@L84352`, `requirement.22.ImportUsingHygiene@L84365`, `rule.22.CtEval-Quote@L84378`, `requirement.22.QuoteBuildSpliceOrder@L84394`, `requirement.22.EmissionOrder@L84408`
- `requirement.22.QuoteSpliceEmissionLowering@L84425`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84440`, `grammar.22.DeriveTargetsAndContracts@L84457`, `def.22.DeriveParseJudg@L84478`, `requirement.22.DeriveAttributeParsingReference@L84491`, `rule.22.Parse-DeriveTargetDecl@L84504`, `rule.22.Parse-DeriveContractOpt-None@L84520`, `rule.22.Parse-DeriveContractOpt-Yes@L84536`
- `rule.22.Parse-DeriveClauseList-Cons@L84552`, `rule.22.Parse-DeriveClause-Requires@L84568`, `rule.22.Parse-DeriveClause-Emits@L84584`, `rule.22.Parse-DeriveClauseTail-End@L84600`, `rule.22.Parse-DeriveClauseTail-Comma@L84616`, `def.22.DeriveTargetDecl@L84634`, `def.22.DeriveGraphAndOrder@L84649`, `requirement.22.DeriveAttributeTargetKinds@L84673`
- `requirement.22.DeriveTargetNameResolution@L84686`, `requirement.22.DeriveTargetBodyBindings@L84699`, `requirement.22.DeriveTargetBodyRestrictions@L84716`, `requirement.22.DeriveExecutionOrder@L84729`, `requirement.22.DeriveOrderTieBreaker@L84744`, `requirement.22.DeriveRequiresValidation@L84757`, `requirement.22.DeriveEmitsValidation@L84770`, `requirement.22.DeriveRequiresEmitsScope@L84783`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L84798`, `rule.22.CtExpandItem-DeriveTargetDecl@L84811`, `rule.22.RunDeriveSet-Empty@L84827`, `rule.22.RunDeriveSet-Cons@L84842`, `rule.22.RunDeriveTarget@L84858`, `def.22.BindDeriveTargetInputs@L84874`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L84887`, `requirement.22.DeriveTargetExecutionTiming@L84903`
- `requirement.22.DeriveTargetFailureSemantics@L84916`, `requirement.22.DeriveTargetsLowering@L84931`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L84946`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L84961`, `requirement.22.UserDiagnosticBuiltinEmission@L85037`

#### `spec.ffi`

Count: 203 total; 203 required; 0 recommended; 0 informative. Ledger line span: L85054-L88478.

- `requirement.23.FFIBoundaryDefinition@L85054`, `def.23.FFIBoundary@L85067`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85084`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85099`, `def.23.FfiSafeTypePredicateAstForm@L85114`, `def.23.FfiSafePredicateMeaning@L85129`, `def.23.FfiSafeJudgements@L85142`, `def.23.FfiPrimitiveTypes@L85155`
- `def.23.FfiLayoutAndPayloadHelpers@L85168`, `def.23.FfiTypeParameterSetHelper@L85184`, `def.23.FfiAliasHelpers@L85197`, `def.23.TypeSubst@L85211`, `def.23.TypeParamsIn@L85248`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85286`, `def.23.FfiSafePredicateClauseHelpers@L85300`, `def.23.ProhibitedFfiType@L85314`
- `def.23.FfiByValueHelpers@L85345`, `rule.23.FfiSafe-Prim@L85360`, `rule.23.FfiSafe-RawPtr@L85376`, `rule.23.FfiSafe-Array@L85392`, `rule.23.FfiSafe-Func@L85408`, `rule.23.FfiSafe-Perm@L85424`, `rule.23.FfiSafe-Alias@L85440`, `rule.23.FfiSafe-Alias-Apply@L85456`
- `rule.23.FfiSafe-Record@L85472`, `rule.23.FfiSafe-Record-Apply@L85488`, `rule.23.FfiSafe-Enum@L85504`, `rule.23.FfiSafe-Enum-Apply@L85520`, `rule.23.FfiSafe-Prohibited-Err@L85536`, `rule.23.FfiSafe-Record-LayoutC-Err@L85552`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85568`, `rule.23.FfiSafe-Record-Field-Err@L85584`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85600`, `rule.23.FfiSafe-Enum-Field-Err@L85616`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L85632`, `rule.23.FfiSafe-Incomplete-Err@L85648`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L85664`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L85680`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L85696`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L85712`
- `requirement.23.FfiSafeProhibitedCategories@L85728`, `requirement.23.FfiSafeRaiiByValueRule@L85755`, `requirement.23.FfiSafeGenericBounds@L85768`, `requirement.23.FfiSafeDynamicSemantics@L85783`, `requirement.23.FfiSafeLowering@L85798`, `diagnostics.23.FfiSafeDiagnostics@L85813`, `grammar.23.ExternProcedureDecl@L85839`, `rule.23.Parse-ExternProcDecl@L85856`
- `ast.23.ExternProcDeclForm@L85874`, `def.23.ExternProcedureDerivedForms@L85891`, `def.23.ExternProcedureMeaning@L85911`, `def.23.ExternAbiStrings@L85924`, `def.23.ExternSignatureRequirements@L85946`, `requirement.23.ExternFfiConstraints@L85969`, `requirement.23.ExternCallSafety@L85986`, `requirement.23.ExternDynamicSemantics@L86003`
- `requirement.23.ExternLowering@L86018`, `diagnostics.23.ExternProcedureDiagnostics@L86033`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86049`, `requirement.23.RawExportedProcedureClassification@L86066`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86081`, `requirement.23.RawExportParsingClassification@L86094`, `ast.23.RawExportProcedureForm@L86109`, `def.23.RawExportedProcedureMeaning@L86126`
- `def.23.ZeroValueHelpers@L86139`, `def.23.ExportSignatureHelpers@L86156`, `rule.23.ExportSig-Ok@L86170`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86188`, `requirement.23.RawExportLibraryImageLifecycle@L86201`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86214`, `requirement.23.RawExportLowering@L86229`, `diagnostics.23.RawExportDiagnostics@L86244`
- `diagnostics.23.RawExportDiagnosticOwnership@L86260`, `requirement.23.HostedExportClassification@L86275`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86290`, `requirement.23.HostedExportParsingClassification@L86303`, `ast.23.HostedExportProcedureForm@L86318`, `def.23.HostedExportProcedureHelpers@L86331`, `requirement.23.HostedRootCapsMeaning@L86358`, `def.23.HostedExportMeaning@L86373`
- `requirement.23.HostedExportForeignVisibleSignature@L86386`, `requirement.23.HostedExportForeignVisiblePassKind@L86399`, `def.23.HostExportSignatureJudgements@L86412`, `rule.23.HostExportSig-Ok@L86425`, `rule.23.HostExport-Library-Err@L86441`, `rule.23.HostExport-MixedMode-Err@L86457`, `rule.23.HostExport-Generic-Err@L86473`, `rule.23.HostExport-Context-Err@L86489`
- `rule.23.HostExport-Context-Raw-Err@L86505`, `rule.23.HostExport-Context-Move-Err@L86521`, `requirement.23.HostedExportSessionHandleValidity@L86539`, `requirement.23.HostedExportCapabilityIsolation@L86552`, `requirement.23.HostedSessionRootCapsGrant@L86565`, `requirement.23.HostedExportBoundaryEntrySequence@L86578`, `requirement.23.HostedExportInvalidHandleBehavior@L86597`, `requirement.23.HostedExportCatchFailureReturn@L86613`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L86628`, `requirement.23.HostedExportThunkAbiDetermination@L86641`, `def.23.HostThunkCarrierHelpers@L86659`, `rule.23.HostThunkParamCarrier-ByRef@L86687`, `rule.23.HostThunkParamCarrier-ByValue-Default@L86703`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L86719`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L86735`, `rule.23.HostThunkRetCarrier-Default@L86751`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L86767`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L86783`, `requirement.23.HostedExportThunkShapeUse@L86799`, `requirement.23.HostedExportNoWin64AggregateSplitting@L86812`, `requirement.23.HostedExportNoExtraAbiRewriting@L86825`, `requirement.23.HostedThunkModeIndependentForeignClassification@L86838`, `requirement.23.HostedThunkToSourceCallReconstruction@L86851`, `requirement.23.HostedStateSymbolResolution@L86864`
- `requirement.23.HostedLibraryLifecycleExports@L86877`, `requirement.23.HostedLifecycleExportsBackendGenerated@L86894`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L86907`, `requirement.23.HostedSessionHandleNoReissue@L86920`, `requirement.23.HostedExportThunkForeignVisibleAbi@L86933`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L86952`, `diagnostics.23.HostedExportDiagnostics@L86967`, `diagnostics.23.HostedExportDiagnosticOwnership@L86986`
- `grammar.23.FfiAttributes@L87003`, `requirement.23.FfiAttributesParsing@L87032`, `ast.23.FfiAttributesAttachedEntries@L87047`, `ast.23.FfiAttributeTargets@L87060`, `requirement.23.MangleAttributeSemantics@L87084`, `def.23.LibraryLinkKinds@L87103`, `requirement.23.LibraryAttributeSemantics@L87123`, `def.23.ResolveLibraryName@L87140`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87167`, `requirement.23.RawDylibResolution@L87180`, `def.23.UnwindModes@L87198`, `requirement.23.UnwindDefaultMode@L87216`, `requirement.23.UnwindAttributeTargetValidity@L87229`, `requirement.23.UnwindCatchAbiRequirement@L87242`, `requirement.23.ExportAttributeSemantics@L87262`, `requirement.23.HostExportAttributeSemantics@L87282`
- `requirement.23.FfiPassByValueAttributeSemantics@L87304`, `requirement.23.FfiAttributeConstraints@L87317`, `requirement.23.FfiAttributesDynamicSemantics@L87343`, `requirement.23.FfiAttributesLowering@L87358`, `diagnostics.23.FfiAttributeDiagnostics@L87373`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87406`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87421`, `ast.23.CapabilityIsolationNoDedicatedAst@L87436`
- `requirement.23.CapabilityIsolationSemantics@L87451`, `def.23.CapabilityIsolationHelpers@L87467`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87484`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87502`, `requirement.23.CapabilityIsolationDynamicSemantics@L87522`, `requirement.23.CapabilityIsolationLowering@L87537`, `diagnostics.23.CapabilityIsolationDiagnostics@L87552`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87567`
- `grammar.23.ForeignContracts@L87584`, `def.23.ForeignContractStart@L87609`, `rule.23.Parse-ForeignContractClauseListOpt-None@L87622`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L87638`, `rule.23.Parse-ForeignContractClauseList-Cons@L87654`, `rule.23.Parse-ForeignContractClauseListTail-End@L87670`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L87686`, `rule.23.Parse-ForeignContractClause-Assumes@L87702`
- `rule.23.Parse-ForeignContractClause-Ensures@L87718`, `def.23.ForeignEnsuresKindAndExpr@L87734`, `rule.23.Parse-EnsuresPredicate-Error@L87753`, `rule.23.Parse-EnsuresPredicate-NullResult@L87769`, `rule.23.Parse-EnsuresPredicate-Plain@L87785`, `ast.23.ForeignContractsForm@L87802`, `ast.23.EnsuresPredicateForms@L87825`, `def.23.ForeignPreconditions@L87847`
- `requirement.23.ForeignPredicateContext@L87860`, `def.23.ForeignPreconditionVerificationModes@L87886`, `requirement.23.ForeignPreconditionVerificationLowering@L87904`, `def.23.ForeignPostconditions@L87919`, `requirement.23.ForeignPostconditionPredicateBindings@L87932`, `def.23.ForeignPostconditionClassification@L87952`, `requirement.23.NullResultWellFormedness@L87985`, `def.23.NullableFfiResult@L88001`
- `rule.23.ForeignEnsures-NullResult-Err@L88017`, `requirement.23.ErrorPredicateWellFormedness@L88036`, `def.23.ForeignPostconditionVerificationModes@L88049`, `requirement.23.ForeignPostconditionStaticVerification@L88067`, `def.23.ForeignContractVerificationSummary@L88082`, `requirement.23.ForeignPreconditionDynamicFailure@L88102`, `requirement.23.ForeignPostconditionDynamicChecks@L88115`, `requirement.23.ForeignContractsLowering@L88130`
- `diagnostics.23.ForeignContractDiagnostics@L88145`, `requirement.23.BoundaryUnwindingSyntax@L88172`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88187`, `ast.23.BoundaryUnwindPolicySource@L88202`, `def.23.UnwindModeAstHelpers@L88215`, `def.23.DetermineUnwindMode@L88237`, `def.23.ParseUnwindArg@L88262`, `rule.23.UnwindMode-Valid@L88284`
- `rule.23.UnwindMode-Invalid-Err@L88302`, `requirement.23.BoundaryUnwindDynamicEffects@L88322`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88341`, `def.23.BoundaryUnwindCodeGenerationEffects@L88356`, `rule.23.CodeGen-UnwindAbort-Import@L88376`, `rule.23.CodeGen-UnwindCatch-Import@L88394`, `rule.23.CodeGen-UnwindAbort-Export@L88412`, `rule.23.CodeGen-UnwindCatch-Export@L88430`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88450`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88463`, `diagnostics.23.FfiDiagnosticsSupplement@L88478`

#### `spec.lowering`

Count: 158 total; 158 required; 0 recommended; 0 informative. Ledger line span: L88499-L91244.

- `requirement.24.SharedLoweringScope@L88499`, `def.24.CodegenModelAndTargets@L88514`, `def.24.CodegenJudgements@L88533`, `def.24.IRDefined@L88546`, `def.24.CodegenCorrectnessPredicates@L88559`, `def.24.CodegenCorrectAndUndefined@L88576`, `def.24.IRFormsAndEmissionJudgements@L88593`, `def.24.PanicOutCodegenParams@L88611`
- `def.24.MethodAndTransitionParams@L88625`, `def.24.SeqIR@L88643`, `def.24.EvalOrderJudgements@L88658`, `def.24.ChildExpressionListHelpers@L88671`, `def.24.ChildrenLTRExpressions@L88717`, `def.24.LowerExprJudgementsAndRetType@L88768`, `rule.24.Lower-Expr-Correctness@L88783`, `def.24.LowerExprTotal@L88799`
- `def.24.ExecIRJudgements@L88813`, `rule.24.ExecIR-ReadVar@L88826`, `rule.24.ExecIR-ReadPath@L88842`, `rule.24.ExecIR-StoreVar@L88858`, `rule.24.ExecIR-StoreVarNoDrop@L88874`, `rule.24.ExecIR-BindVar@L88890`, `rule.24.ExecIR-ReadPtr@L88906`, `rule.24.ExecIR-WritePtr@L88922`
- `def.24.AllocTarget@L88938`, `rule.24.ExecIR-Alloc@L88952`, `rule.24.MoveState-Root@L88968`, `rule.24.MoveState-Field@L88984`, `rule.24.ExecIR-MoveState@L89000`, `def.24.ExecIRControlResults@L89016`, `rule.24.ExecIR-Defer@L89032`, `def.24.ExecIRBlockHelpers@L89048`
- `rule.24.ExecIR-If-True@L89063`, `rule.24.ExecIR-If-False@L89079`, `rule.24.ExecIR-Block@L89095`, `rule.24.ExecIR-IfCase@L89111`, `rule.24.ExecIR-Loop-Infinite-Step@L89127`, `rule.24.ExecIR-Loop-Infinite-Continue@L89143`, `rule.24.ExecIR-Loop-Infinite-Break@L89159`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89175`
- `rule.24.ExecIR-Loop-Cond-False@L89191`, `rule.24.ExecIR-Loop-Cond-True-Step@L89207`, `rule.24.ExecIR-Loop-Cond-Continue@L89223`, `rule.24.ExecIR-Loop-Cond-Break@L89239`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89255`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89271`, `def.24.LoopIterIRJudgement@L89287`, `rule.24.ExecIR-Loop-Iter@L89300`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89316`, `rule.24.LoopIterIR-Done@L89332`, `rule.24.LoopIterIR-Step-Val@L89348`, `rule.24.LoopIterIR-Step-Continue@L89364`, `rule.24.LoopIterIR-Step-Break@L89380`, `rule.24.LoopIterIR-Step-Ctrl@L89396`, `rule.24.ExecIR-Region@L89412`, `rule.24.ExecIR-Frame-Implicit@L89428`
- `rule.24.ExecIR-Frame-Explicit@L89444`, `rule.24.LowerList-Empty@L89460`, `rule.24.LowerList-Cons@L89475`, `rule.24.LowerFieldInits-Empty@L89491`, `rule.24.LowerFieldInits-Cons@L89506`, `rule.24.LowerOpt-None@L89522`, `rule.24.LowerOpt-Some@L89537`, `def.24.RefSyms@L89553`
- `def.24.ExpandIR@L89615`, `def.24.UniqueEmits@L89628`, `def.24.ModuleItems@L89650`, `rule.24.CG-Project@L89663`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L89679`, `rule.24.CG-Module@L89692`, `rule.24.CG-Expr@L89708`, `rule.24.CG-Stmt@L89724`
- `rule.24.CG-Block@L89740`, `rule.24.CG-Place@L89756`, `rule.24.LowerIR-Module@L89772`, `rule.24.LowerIR-Err@L89788`, `rule.24.EmitLLVM-Ok@L89804`, `def.24.LLVMText21Acceptance@L89820`, `rule.24.EmitLLVM-Err@L89834`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L89850`
- `rule.24.EmitObj-Ok@L89864`, `def.24.LLVMEmitObj21@L89880`, `rule.24.EmitObj-Err@L89893`, `def.24.PointerPrimitiveSizeAndAlignment@L89913`, `def.24.LayoutJudgements@L89968`, `rule.24.Size-Prim@L89981`, `rule.24.Align-Prim@L89997`, `rule.24.Layout-Prim@L90013`
- `def.24.ConstantEncodingHelpers@L90029`, `rule.24.Encode-Bool@L90046`, `rule.24.Encode-Char@L90062`, `rule.24.Encode-Int@L90078`, `rule.24.Encode-Float@L90094`, `rule.24.Encode-Unit@L90110`, `rule.24.Encode-Never@L90126`, `rule.24.Encode-RawPtr-Null@L90142`
- `def.24.ValidValueJudgement@L90158`, `rule.24.Valid-Bool@L90171`, `rule.24.Valid-Char@L90185`, `rule.24.Valid-Scalar@L90199`, `rule.24.Valid-Unit@L90214`, `rule.24.Valid-Never@L90228`, `def.24.ValidValueFallback@L90242`, `rule.24.Layout-Perm@L90258`
- `rule.24.Size-Perm@L90274`, `rule.24.Align-Perm@L90290`, `def.24.ValueBitsPerm@L90306`, `rule.24.Size-Ptr@L90319`, `rule.24.Align-Ptr@L90335`, `rule.24.Layout-Ptr@L90351`, `rule.24.Size-RawPtr@L90367`, `rule.24.Align-RawPtr@L90383`
- `rule.24.Layout-RawPtr@L90399`, `rule.24.Size-Func@L90415`, `rule.24.Align-Func@L90431`, `rule.24.Layout-Func@L90447`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90465`, `def.24.ExternAbiSetAndConventionMapping@L90545`, `def.24.ConventionLayout@L90567`, `def.24.AssignParamRegs@L90617`
- `def.24.StackFrameForm@L90643`, `rule.24.StackFrame-Layout@L90663`, `rule.24.Conv-Compatible@L90680`, `rule.24.Conv-FFI-Required@L90696`, `def.24.ABITypeAndABITyJudgement@L90714`, `rule.24.ABI-Prim@L90728`, `rule.24.ABI-Perm@L90744`, `rule.24.ABI-Ptr@L90760`
- `rule.24.ABI-RawPtr@L90776`, `rule.24.ABI-Func@L90792`, `rule.24.ABI-Alias@L90808`, `rule.24.ABI-Record@L90824`, `rule.24.ABI-Tuple@L90840`, `rule.24.ABI-Array@L90856`, `rule.24.ABI-Slice@L90872`, `rule.24.ABI-Range@L90888`
- `rule.24.ABI-RangeInclusive@L90904`, `rule.24.ABI-RangeFrom@L90920`, `rule.24.ABI-RangeTo@L90936`, `rule.24.ABI-RangeToInclusive@L90952`, `rule.24.ABI-RangeFull@L90968`, `rule.24.ABI-Enum@L90984`, `rule.24.ABI-Union@L91000`, `rule.24.ABI-Modal@L91016`
- `rule.24.ABI-Dynamic@L91032`, `rule.24.ABI-StringBytes@L91048`, `def.24.ABIParameterReturnPassingHelpers@L91066`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91087`, `rule.24.ABI-Param-ByRef-Alias@L91100`, `rule.24.ABI-Param-ByValue-Move@L91116`, `rule.24.ABI-Param-ByRef-Move@L91132`, `rule.24.ABI-Ret-ByValue@L91148`
- `rule.24.ABI-Ret-ByRef@L91164`, `rule.24.ABI-Call@L91180`, `rule.24.ABI-ForeignParam-ByValue@L91196`, `rule.24.ABI-ForeignParam-ByRef@L91212`, `rule.24.ABI-ForeignCall@L91228`, `def.24.PanicRecordAndPanicOut@L91244`

#### `spec.symbols`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L91273-L92088.

- `def.24.MangleJudgementAndConstructors@L91273`, `def.24.PathSymbolHelpers@L91289`, `def.24.ItemPath@L91306`, `def.24.PathOfTypeAndClassPath@L91328`, `def.24.LiteralSymbolHashing@L91349`, `def.24.ScopedRawAndHostBodySymbols@L91368`, `def.24.AttributeSymbolHelpers@L91383`, `def.24.ExternAbiSymbolHelpers@L91403`
- `def.24.LinkName@L91422`, `def.24.HostThunkLinkNameAndItemName@L91440`, `rule.24.Mangle-HostExport-Proc@L91457`, `rule.24.Mangle-Proc@L91473`, `rule.24.Mangle-ExternProc@L91489`, `rule.24.Mangle-Main@L91505`, `rule.24.Mangle-Record-Method@L91521`, `rule.24.Mangle-Class-Method@L91537`
- `rule.24.Mangle-State-Method@L91553`, `rule.24.Mangle-Transition@L91569`, `rule.24.Mangle-Static@L91585`, `rule.24.Mangle-StaticBinding@L91601`, `rule.24.Mangle-VTable@L91617`, `rule.24.Mangle-Literal@L91633`, `rule.24.Mangle-DefaultImpl@L91649`, `req.24.ClosureIndexUniqueness@L91665`
- `def.24.EnclosingSym@L91678`, `rule.24.Mangle-Closure@L91691`, `rule.24.Mangle-ClosureEnv@L91707`, `def.24.ClosureCodeSym@L91723`, `def.24.LinkageDefinitions@L91738`, `rule.24.Linkage-UserItem@L91752`, `rule.24.Linkage-ExternProc@L91768`, `rule.24.Linkage-UserItem-Internal@L91784`
- `rule.24.Linkage-StaticBinding@L91800`, `rule.24.Linkage-StaticBinding-Internal@L91816`, `rule.24.Linkage-ClassMethod@L91832`, `rule.24.Linkage-ClassMethod-Internal@L91848`, `rule.24.Linkage-StateMethod@L91864`, `rule.24.Linkage-StateMethod-Internal@L91880`, `rule.24.Linkage-Transition@L91896`, `rule.24.Linkage-Transition-Internal@L91912`
- `rule.24.Linkage-InitFn@L91928`, `rule.24.Linkage-DeinitFn@L91944`, `rule.24.Linkage-VTable@L91960`, `rule.24.Linkage-LiteralData@L91976`, `rule.24.Linkage-DropGlue@L91992`, `rule.24.Linkage-DefaultImpl@L92008`, `rule.24.Linkage-DefaultImpl-Internal@L92024`, `rule.24.Linkage-PanicSym@L92040`
- `rule.24.Linkage-BuiltinModalSym@L92056`, `rule.24.Linkage-BuiltinSym@L92072`, `rule.24.Linkage-EntrySym@L92088`

#### `spec.initialization`

Count: 102 total; 102 required; 0 recommended; 0 informative. Ledger line span: L92108-L93623.

- `def.24.GlobalsJudg@L92108`, `def.24.ConstInitJudg@L92121`, `def.24.ConstInitLiteral@L92134`, `def.24.StaticName@L92147`, `def.24.StaticBindTypes@L92162`, `def.24.StaticBindList@L92175`, `def.24.StaticBinding@L92188`, `def.24.StaticSym@L92201`
- `rule.24.Emit-Static-Const@L92216`, `rule.24.Emit-Static-Init@L92232`, `rule.24.Emit-Static-Multi@L92248`, `def.24.InitSym@L92264`, `rule.24.InitFn@L92277`, `def.24.DeinitSym@L92293`, `rule.24.DeinitFn@L92306`, `def.24.StaticItems@L92322`
- `def.24.StaticItemOf@L92335`, `def.24.StaticSymPath@L92348`, `def.24.StaticAddr@L92361`, `req.24.HostedStaticAddrSessionInterpretation@L92374`, `def.24.AddrOfSym@L92387`, `def.24.StaticType@L92400`, `def.24.StaticBindInfo@L92413`, `def.24.SeqIRList@L92426`
- `def.24.StaticStoreIR@L92440`, `rule.24.Lower-StaticInit-Item@L92454`, `rule.24.Lower-StaticInitItems-Empty@L92470`, `rule.24.Lower-StaticInitItems-Cons@L92485`, `rule.24.Lower-StaticInit@L92501`, `rule.24.InitCallIR@L92517`, `def.24.Rev@L92533`, `rule.24.Lower-StaticDeinitNames-Empty@L92547`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92562`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92578`, `rule.24.Lower-StaticDeinit-Item@L92594`, `rule.24.Lower-StaticDeinitItems-Empty@L92610`, `rule.24.Lower-StaticDeinitItems-Cons@L92625`, `rule.24.Lower-StaticDeinit@L92641`, `rule.24.DeinitCallIR@L92657`, `def.24.HostedStateAddressDefinitions@L92673`
- `def.24.LibraryStateSymbolDefinitions@L92688`, `def.24.HostedStateJudg@L92704`, `req.24.SessionStateInitDefinesHostedCells@L92717`, `req.24.SessionStateDestroyRemovesHostedCells@L92730`, `req.24.HostedLibraryStateAddressInterpretation@L92743`, `def.24.InitializationGraphOrdering@L92760`, `rule.24.Topo-Ok@L92779`, `rule.24.Topo-Cycle@L92795`
- `def.24.ProjectInitializationItems@L92811`, `def.24.InitializationPlanDefinitions@L92827`, `def.24.EvalFromEvalSigma@L92849`, `rule.24.EmitInitPlan@L92863`, `rule.24.EmitInitPlan-Err@L92879`, `rule.24.EmitDeinitPlan@L92895`, `rule.24.EmitDeinitPlan-Err@L92911`, `def.24.InitStateMachineDefinitions@L92927`
- `rule.24.Init-Start@L92942`, `rule.24.Init-Step@L92957`, `rule.24.Init-Next-Module@L92973`, `rule.24.Init-Panic@L92989`, `rule.24.Init-Done@L93005`, `rule.24.Init-Ok@L93021`, `rule.24.Init-Fail@L93037`, `rule.24.Deinit-Ok@L93053`
- `rule.24.Deinit-Panic@L93069`, `def.24.EntryJudg@L93087`, `rule.24.EntrySym-Decl@L93100`, `rule.24.ContextInitSym-Decl@L93115`, `def.24.PanicRecordInit@L93130`, `def.24.EntryStubSpec@L93143`, `rule.24.EntryStub-Decl@L93161`, `rule.24.EntrySym-Err@L93177`
- `rule.24.EntryStub-Err@L93193`, `def.24.LibraryImageJudg@L93211`, `def.24.LibraryImageStateDefinitions@L93224`, `req.24.DistinctLibraryImageState@L93244`, `req.24.LibraryImageLivenessTransitions@L93257`, `req.24.LibraryImageInitDefinesSharedCells@L93270`, `req.24.LibraryImageDestroyRemovesSharedCells@L93283`, `req.24.SharedLibraryImageStateInterpretation@L93296`
- `req.24.PartialInitPanicCleanupPrefix@L93309`, `req.24.RawExportImageLifecycle@L93322`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93335`, `req.24.SharedLibraryLoaderEntrypoint@L93348`, `rule.24.LibraryImageInitSigma@L93361`, `rule.24.RawLibraryCallSigma-Ok@L93377`, `rule.24.LibraryImageDestroySigma@L93393`, `def.24.HostedSessionJudg@L93409`
- `def.24.HostedSessionStateDefinitions@L93422`, `req.24.DistinctHostedState@L93444`, `req.24.HostedSessionLifecycleState@L93457`, `req.24.HostedSessionNoConcurrentReentry@L93470`, `rule.24.HostSessionInitSigma@L93483`, `rule.24.HostedCallSigma-Ok@L93499`, `rule.24.HostSessionDestroySigma@L93515`, `def.24.InterpJudg@L93533`
- `def.24.ContextValue@L93546`, `rule.24.ContextInitSigma@L93559`, `rule.24.Interpret-Project-Ok@L93575`, `rule.24.Interpret-Project-Init-Panic@L93591`, `rule.24.Interpret-Project-Main-Ctrl@L93607`, `rule.24.Interpret-Project-Deinit-Panic@L93623`

#### `spec.cleanup`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L93645-L94497.

- `def.24.CleanupJudg@L93645`, `rule.24.CleanupPlan@L93658`, `def.24.EmitDropSpec@L93674`, `def.24.PanicOutAddr@L93690`, `def.24.PanicRecordOf@L93703`, `def.24.WritePanicRecord@L93716`, `def.24.InitPanicHandle@L93729`, `req.24.InitPanicHandleResponsiblePrefix@L93742`
- `rule.24.PanicSym@L93755`, `def.24.PanicReasonCodes@L93770`, `def.24.PanicSites@L93795`, `def.24.ClearPanic@L93819`, `def.24.PanicCheck@L93832`, `def.24.LowerPanic@L93845`, `def.24.ResponsibleBinding@L93860`, `grammar.24.CleanupItem@L93873`
- `def.24.DropJudgmentDefinitions@L93886`, `def.24.RecordType@L93903`, `def.24.DropCall@L93916`, `def.24.ReleaseValue@L93933`, `def.24.DropChildren@L93947`, `def.24.DropList@L93967`, `rule.24.DropAction-Moved@L93981`, `rule.24.DropAction-Partial@L93997`
- `rule.24.DropAction-Valid@L94013`, `rule.24.DropStaticAction@L94029`, `def.24.NonRecordFOk@L94045`, `rule.24.DropValueOut-DropPanic@L94058`, `rule.24.DropValueOut-ChildPanic@L94074`, `rule.24.DropValueOut-Ok@L94090`, `def.24.CleanupStateDefinitions@L94108`, `rule.24.Cleanup-Start@L94122`
- `rule.24.Cleanup-Step-Drop-Ok@L94137`, `rule.24.Cleanup-Step-Drop-Panic@L94153`, `rule.24.Cleanup-Step-Drop-Abort@L94169`, `rule.24.Cleanup-Step-DropStatic-Ok@L94185`, `rule.24.Cleanup-Step-DropStatic-Panic@L94201`, `rule.24.Cleanup-Step-DropStatic-Abort@L94217`, `rule.24.Cleanup-Step-Defer-Ok@L94233`, `rule.24.Cleanup-Step-Defer-Panic@L94249`
- `rule.24.Cleanup-Step-Defer-Abort@L94265`, `rule.24.Cleanup-Done@L94281`, `rule.24.Destroy-Empty@L94297`, `rule.24.Destroy-Cons@L94312`, `def.24.CleanupJudgDyn@L94328`, `rule.24.Cleanup-Empty@L94341`, `rule.24.Cleanup-Cons-Drop@L94356`, `rule.24.Cleanup-Cons-Drop-Panic@L94372`
- `rule.24.Cleanup-Cons-DropStatic@L94388`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94404`, `rule.24.Cleanup-Cons-Defer-Ok@L94420`, `rule.24.Cleanup-Cons-Defer-Panic@L94436`, `def.24.CleanupScopeJudg@L94452`, `rule.24.CleanupScope-From-SmallStep@L94465`, `rule.24.Unwind-Step@L94481`, `rule.24.Unwind-Abort@L94497`

#### `spec.runtime-interface`

Count: 64 total; 64 required; 0 recommended; 0 informative. Ledger line span: L94519-L95514.

- `def.24.RuntimeIfcJudg@L94519`, `def.24.BuiltinModalLayoutSpec@L94532`, `rule.24.BuiltinModalLayout@L94545`, `def.24.BuiltinModalSymMap@L94561`, `rule.24.BuiltinModalSym@L94588`, `rule.24.RegionAddr-AddrIsActive@L94604`, `rule.24.RegionAddr-AddrTagFrom@L94619`, `rule.24.BuiltinSym-FileSystem-OpenRead@L94634`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L94649`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L94664`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L94679`, `rule.24.BuiltinSym-FileSystem-ReadFile@L94694`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L94709`, `rule.24.BuiltinSym-FileSystem-WriteFile@L94724`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L94739`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L94754`
- `rule.24.BuiltinSym-FileSystem-Exists@L94769`, `rule.24.BuiltinSym-FileSystem-Remove@L94784`, `rule.24.BuiltinSym-FileSystem-OpenDir@L94799`, `rule.24.BuiltinSym-FileSystem-CreateDir@L94814`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L94829`, `rule.24.BuiltinSym-FileSystem-Kind@L94844`, `rule.24.BuiltinSym-FileSystem-Restrict@L94859`, `rule.24.BuiltinSym-Network-RestrictHost@L94874`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L94889`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L94904`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L94919`, `rule.24.BuiltinSym-Reactor-Run@L94934`, `rule.24.BuiltinSym-Reactor-Register@L94949`, `rule.24.BuiltinSym-System-Exit@L94964`, `rule.24.BuiltinSym-System-GetEnv@L94979`, `rule.24.BuiltinSym-System-Run@L94994`
- `def.24.BuiltinSymJudg@L95011`, `def.24.StringBytesBuiltinMethodSets@L95024`, `def.24.StringBuiltinSymbols@L95040`, `def.24.BytesBuiltinSymbols@L95059`, `rule.24.BuiltinSym-String-Err@L95081`, `rule.24.BuiltinSym-Bytes-Err@L95097`, `def.24.DropHookJudg@L95113`, `rule.24.StringDropSym-Decl@L95126`
- `rule.24.BytesDropSym-Decl@L95141`, `rule.24.StringDropSym-Err@L95156`, `rule.24.BytesDropSym-Err@L95172`, `def.24.RuntimeDeclJudg@L95190`, `def.24.RuntimeMethodAndSymbolSets@L95203`, `def.24.CapabilityBuiltinSigs@L95222`, `def.24.CoreRuntimeSigs@L95240`, `def.24.BuiltinModalProcSigs@L95256`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95274`, `def.24.LLVMDeclType@L95293`, `rule.24.RuntimeDecls@L95306`, `def.24.RuntimeDeclarationCoverage@L95322`, `rule.24.Prim-Network-RestrictHost-Runtime@L95341`, `def.24.HeapJudg@L95357`, `req.24.HeapHostPrimitiveRelations@L95370`, `def.24.HeapStateAccountingDefinitions@L95383`
- `req.24.HeapPrimitiveSemantics@L95398`, `rule.24.Prim-Heap-WithQuota@L95424`, `rule.24.Prim-Heap-AllocRaw@L95440`, `rule.24.Prim-Heap-DeallocRaw@L95456`, `def.24.ReactorJudg@L95472`, `req.24.ReactorHostPrimitiveRelations@L95485`, `rule.24.Prim-Reactor-Run@L95498`, `rule.24.Prim-Reactor-Register@L95514`

#### `spec.backend`

Count: 190 total; 190 required; 0 recommended; 0 informative. Ledger line span: L95536-L98637.

- `def.24.LLVMHeader@L95536`, `def.24.OpaquePointerModel@L95551`, `def.24.LLVMAttrJudg@L95573`, `rule.24.PtrStateOf-Perm@L95586`, `rule.24.LLVM-PtrAttrs-Valid@L95602`, `rule.24.LLVM-PtrAttrs-Other@L95618`, `rule.24.LLVM-PtrAttrs-RawPtr@L95634`, `rule.24.LLVM-ArgAttrs-Ptr@L95650`
- `rule.24.LLVM-ArgAttrs-RawPtr@L95667`, `rule.24.LLVM-ArgAttrs-NonPtr@L95683`, `def.24.LLVMOptionalArgumentAttrs@L95699`, `def.24.LLVMUBAndPoisonAvoidance@L95717`, `def.24.MemoryIntrinsics@L95744`, `def.24.LLVMToolchain@L95767`, `req.24.HostedCompilerLLVMVersion@L95780`, `def.24.LLVMTyJudg@L95795`
- `def.24.LLVMPrimitiveTypeHelpers@L95808`, `def.24.StructElems@L95844`, `def.24.TaggedElems@L95862`, `rule.24.LLVMTy-Prim@L95879`, `rule.24.LLVMTy-Perm@L95895`, `rule.24.LLVMTy-Refine@L95911`, `rule.24.LLVMTy-Ptr@L95927`, `rule.24.LLVMTy-RawPtr@L95943`
- `rule.24.LLVMTy-Func@L95959`, `rule.24.LLVMTy-Closure@L95975`, `rule.24.LLVMTy-Alias@L95991`, `rule.24.LLVMTy-Record@L96007`, `rule.24.LLVMTy-Tuple@L96023`, `rule.24.LLVMTy-Array@L96039`, `rule.24.LLVMTy-Slice@L96055`, `rule.24.LLVMTy-Range@L96071`
- `rule.24.LLVMTy-RangeInclusive@L96087`, `rule.24.LLVMTy-RangeFrom@L96103`, `rule.24.LLVMTy-RangeTo@L96119`, `rule.24.LLVMTy-RangeToInclusive@L96135`, `rule.24.LLVMTy-RangeFull@L96151`, `rule.24.LLVMTy-Enum@L96166`, `rule.24.LLVMTy-Union-Niche@L96182`, `rule.24.LLVMTy-Union-Tagged@L96198`
- `rule.24.LLVMTy-Modal-Niche@L96214`, `rule.24.LLVMTy-Modal-Tagged@L96230`, `rule.24.LLVMTy-Modal-StringBytes@L96246`, `rule.24.LLVMTy-ModalState@L96264`, `rule.24.LLVMTy-Dynamic@L96280`, `rule.24.LLVMTy-StringView@L96296`, `rule.24.LLVMTy-StringManaged@L96312`, `rule.24.LLVMTy-BytesView@L96328`
- `rule.24.LLVMTy-BytesManaged@L96344`, `rule.24.LLVMTy-Err@L96360`, `def.24.LowerIRJudg@L96378`, `def.24.LLVMInstrHelpers@L96391`, `rule.24.LowerIRInstr-Empty@L96422`, `rule.24.LowerIRInstr-Seq@L96437`, `def.24.MemoryInstructionHelpers@L96453`, `def.24.ConstBytesEncoding@L96470`
- `def.24.StaticTypeBySymbol@L96496`, `def.24.StateRefJudg@L96510`, `rule.24.StateRef-Session@L96524`, `rule.24.StateRef-Global@L96540`, `def.24.CallSignatureHelpers@L96556`, `def.24.ParamInitHelpers@L96575`, `rule.24.LowerIRDecl-Proc-User@L96599`, `rule.24.LowerIRDecl-Proc-Gen@L96615`
- `rule.24.LowerIRDecl-GlobalConst@L96631`, `rule.24.LowerIRDecl-GlobalZero@L96647`, `req.24.HostedStateInitializerTemplates@L96663`, `rule.24.LowerIRDecl-VTable@L96676`, `rule.24.Lower-AllocIR@L96692`, `rule.24.Lower-BindVarIR@L96708`, `rule.24.Lower-ReadVarIR@L96724`, `rule.24.Lower-ReadVarIR-Err@L96740`
- `def.24.ProcSymbol@L96756`, `rule.24.Lower-ReadPathIR-Static-User@L96769`, `rule.24.Lower-ReadPathIR-Static-Gen@L96785`, `rule.24.Lower-ReadPathIR-Proc-User@L96801`, `rule.24.Lower-ReadPathIR-Proc-Gen@L96817`, `rule.24.Lower-ReadPathIR-Runtime@L96833`, `rule.24.Lower-ReadPathIR-Record@L96849`, `rule.24.Lower-StoreVarIR@L96865`
- `rule.24.Lower-StoreVarNoDropIR@L96881`, `rule.24.Lower-MoveStateIR@L96897`, `rule.24.Lower-StoreGlobal@L96913`, `rule.24.Lower-ReadPlaceIR@L96929`, `rule.24.Lower-WritePlaceIR@L96945`, `def.24.PtrType@L96961`, `rule.24.Lower-ReadPtrIR@L96974`, `rule.24.Lower-ReadPtrIR-Raw@L96990`
- `rule.24.Lower-ReadPtrIR-Null@L97006`, `rule.24.Lower-ReadPtrIR-Expired@L97022`, `rule.24.Lower-WritePtrIR@L97038`, `rule.24.Lower-WritePtrIR-Null@L97054`, `rule.24.Lower-WritePtrIR-Expired@L97070`, `rule.24.Lower-WritePtrIR-Raw@L97086`, `rule.24.Lower-WritePtrIR-Raw-Err@L97102`, `rule.24.Lower-AddrOfIR@L97118`
- `def.24.CallLoweringHelpers@L97134`, `rule.24.Lower-CallIR-Func@L97162`, `def.24.DynamicDispatchHelpers@L97178`, `rule.24.Lower-CallVTable@L97196`, `rule.24.LowerIRInstr-ClearPanic@L97212`, `rule.24.LowerIRInstr-PanicCheck@L97228`, `rule.24.LowerIRInstr-CheckPoison@L97244`, `rule.24.LowerIRInstr-LowerPanic@L97260`
- `def.24.IfLoweringHelpers@L97276`, `rule.24.Lower-IfIR@L97293`, `def.24.BlockCleanupLoweringHelpers@L97309`, `rule.24.Lower-BlockIR@L97325`, `def.24.StructuredIRLoweringForms@L97341`, `rule.24.Lower-LoopIR@L97369`, `rule.24.Lower-IfCaseIR@L97385`, `rule.24.Lower-RegionIR@L97401`
- `rule.24.Lower-FrameIR@L97417`, `def.24.BranchLowerForms@L97433`, `rule.24.Lower-BranchIR-Unconditional@L97447`, `rule.24.Lower-BranchIR-Conditional@L97463`, `def.24.PhiLowerForm@L97478`, `rule.24.Lower-PhiIR@L97491`, `rule.24.LowerIRDecl-Err@L97507`, `rule.24.LowerIRInstr-Err@L97523`
- `def.24.BindStorageJudg@L97541`, `def.24.BindRegionTarget@L97564`, `req.24.ResolveTargetNearestLiveAlias@L97583`, `rule.24.BindValid-Sigma@L97596`, `rule.24.BindSlot-Param-ByValue@L97612`, `rule.24.BindSlot-Param-ByRef@L97628`, `rule.24.BindSlot-Region@L97644`, `rule.24.BindSlot-Local@L97660`
- `rule.24.BindSlot-Static@L97676`, `rule.24.UpdateValid-BindVar@L97692`, `rule.24.UpdateValid-StoreVar@L97707`, `rule.24.UpdateValid-StoreVarNoDrop@L97722`, `rule.24.UpdateValid-MoveRoot@L97738`, `rule.24.UpdateValid-PartialMove-Init@L97754`, `rule.24.UpdateValid-PartialMove-Step@L97770`, `def.24.DropOnAssignHelpers@L97786`
- `rule.24.DropOnAssign-NotApplicable@L97802`, `rule.24.DropOnAssign-Record-Valid@L97818`, `rule.24.DropOnAssign-Record-Partial@L97834`, `rule.24.DropOnAssign-Record-Moved@L97850`, `rule.24.DropOnAssign-Aggregate-Ok@L97866`, `rule.24.DropOnAssign-Aggregate-Moved@L97882`, `rule.24.BindSlot-Err@L97898`, `rule.24.BindValid-Err@L97914`
- `rule.24.UpdateValid-Err@L97930`, `rule.24.DropOnAssign-Err@L97946`, `def.24.LLVMCallJudg@L97964`, `def.24.LLVMCallSigFields@L97977`, `rule.24.LLVMArgLower-ByValue-PtrValid@L97993`, `rule.24.LLVMArgLower-ByValue-Other@L98009`, `rule.24.LLVMArgLower-ByRef@L98025`, `rule.24.LLVMRetLower-ByValue-ZST@L98041`
- `rule.24.LLVMRetLower-ByValue@L98057`, `rule.24.LLVMRetLower-SRet@L98073`, `def.24.LLVMCallArgLists@L98089`, `rule.24.LLVMCall-ByValue@L98104`, `rule.24.LLVMCall-SRet@L98120`, `def.24.ByRefAccess@L98136`, `rule.24.LLVMArgLower-Err@L98151`, `rule.24.LLVMRetLower-Err@L98167`
- `rule.24.LLVMCall-Err@L98183`, `def.24.VTableJudg@L98201`, `def.24.VTableEmissionHelpers@L98214`, `rule.24.EmitDropGlue-Decl@L98238`, `rule.24.EmitVTable-Err@L98254`, `def.24.LiteralEmitJudg@L98272`, `def.24.StringBytesAndRawBytes@L98285`, `rule.24.EmitLiteralData-Decl@L98308`
- `rule.24.EmitLiteral-String@L98324`, `req.24.EmitLiteral-String-Utf8Valid@L98340`, `rule.24.EmitLiteral-Bytes@L98353`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98369`, `rule.24.EmitLiteral-Char@L98382`, `rule.24.EmitLiteral-Int@L98398`, `rule.24.EmitLiteral-Float@L98414`, `rule.24.EmitLiteral-Err@L98430`
- `def.24.PoisonJudg@L98448`, `def.24.PoisonSet@L98461`, `rule.24.PoisonFlag-Decl@L98474`, `def.24.PoisonFlagStorage@L98489`, `req.24.HostedPoisonFlagTemplate@L98503`, `rule.24.CheckPoison-Use@L98516`, `sem.24.CheckPoisonBehavior@L98532`, `req.24.HostedPoisonStateIsolation@L98545`
- `rule.24.SetPoison-OnInitFail@L98558`, `rule.24.PoisonFlag-Err@L98574`, `rule.24.CheckPoison-Err@L98590`, `rule.24.SetPoison-Err@L98606`, `req.24.OutputBackendDiagnosticsOwnership@L98624`, `diag.24.OutputBackendDiagnostics@L98637`

### Lowering, Backend, Runtime Interface, And Driver

#### `backend.llvm-target`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L5343-L5373.

- `def.LLVMTargetConstants@L5343`, `def.IsRootModule@L5359`, `def.WithEntry@L5373`

#### `backend.llvm-codegen`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L5389-L6747.

- `CodegenObj-LLVM@L5389`, `CodegenIR-LLVM@L5407`, `AssembleIR-Ok@L6729`, `AssembleIR-Err@L6747`

#### `lowering.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27099-L27308.

- `conformance.AttributeLoweringOwnership@L27099`, `conformance.VendorAttributeLowering@L27308`

#### `lowering.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27595-L27595.

- `conformance.LayoutAttributeLowering@L27595`

#### `lowering.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27740-L27740.

- `conformance.OptimizationAttributeLowering@L27740`

#### `lowering.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28221-L28221.

- `conformance.DiagnosticsMetadataLowering@L28221`

#### `lowering.permissions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L28471-L28686.

- `conformance.PermissionLayoutNeutrality@L28471`, `req.PermissionFormsLoweringDiagnostics@L28533`, `conformance.AliasExclusivityLowering@L28686`

#### `codegen`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L29113-L40918.

- `conformance.PermissionAdmissibilityLowering@L29113`, `conformance.ImportDeclarationLowering@L29348`, `conformance.UsingDeclarationLowering@L29855`, `def.ConstInitJudgementSet@L30148`, `def.ConstInitLiteralEncoding@L30164`, `def.StaticName@L30178`, `def.StaticBindingFunctionSignature@L30222`, `def.StaticSym@L30236`
- `def.InitSym@L30306`, `def.DeinitSym@L30338`, `def.StaticSymPath@L30398`, `def.StaticAddr@L30412`, `def.AddrOfSym@L30440`, `def.SeqIRList@L30482`, `def.StaticStoreIR@L30497`, `def.Rev@L30601`
- `conformance.ExternBlockLowering@L31046`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34243`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34259`, `def.PrimitiveValueBits@L34590`, `req.PrimitiveLayoutAbiOwnership@L34610`, `def.TupleFields@L35034`, `def.TupleLayoutJudgementSet@L35345`, `def.TupleValueBits@L35450`
- `def.ArrayLen@L36139`, `def.ArrayValueBits@L36153`, `def.SliceValueBits@L36708`, `def.LoweringChecksJudgementSet@L37370`, `def.RangeValueBits@L37386`, `def.RecordLayoutHelpers@L38546`, `def.FieldOffset@L38658`, `def.FieldValueList@L38672`
- `def.StructBits@L38686`, `def.PadBytes@L38700`, `def.RecordValueBits@L38714`, `def.EnumLayoutHelpers@L39724`, `def.EnumPayloadBits@L39825`, `def.EnumValueBits@L39841`, `def.UnionNicheOrderingHelpers@L40194`, `def.UnionTypeOrderingKeys@L40214`
- `def.TypeKey@L40256`, `def.TypeKeyOrdering@L40289`, `def.UnionMemberLayoutSelection@L40311`, `def.UnionLayoutHelpers@L40331`, `def.UnionNicheBits@L40440`, `def.UnionPayloadBits@L40454`, `def.TaggedBits@L40468`, `def.UnionTaggedBits@L40484`
- `def.UnionBits@L40498`, `def.UnionValueBits@L40512`, `def.TypeAliasValueBits@L40918`
