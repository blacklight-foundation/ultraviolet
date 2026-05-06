# Ultraviolet Compiler Rebuild Plan

Date: 2026-05-05

This plan lives in the Ultraviolet repository and is the execution contract for rebuilding the full Ultraviolet compiler in Ultraviolet, bootstrapped by the existing Cursive compiler. It is intentionally concrete: implementation must migrate one audited object at a time, but the target architecture is the complete compiler, not a minimal vertical slice.

The authoritative language contract is `SPECIFICATION.md`. The current obligation ledger used by this plan is `Docs/Audit/UltravioletObligations.csv`; the scaffold pass must preserve ledger contents while normalizing final audit/tool names to the PascalCase paths defined below.

## Non-Negotiable Constraints

- `SPECIFICATION.md` is the source of truth. Do not edit it as part of the rebuild unless the user explicitly approves the spec change.
- The rebuild is a full self-hosting compiler implementation. Do not create a partial/minimal compiler architecture and do not mechanically rename C++ files into Ultraviolet.
- Every target directory is PascalCase. Every Ultraviolet source file is `PascalCase.uv` except externally mandated names inside source text, ABI symbols, serialized keys, and the public `uv` command name.
- The Cursive compiler is the bootstrap compiler. It may be patched only for bootstrap blockers or confirmed spec-conformance defects needed to compile Ultraviolet.
- No shim, adapter, duplicate implementation, fallback path, test-only branch, or compatibility layer is allowed unless the specification explicitly defines that boundary.
- Keep the obligation extraction/ledger tooling as the only support tooling; conformance evidence must come from compiler tests, source-native test procedures, traces, and bootstrap outputs.
- Ultraviolet is self-hosted. The Ultraviolet compiler implementation must own its lowering, target-instruction lowering, object emission, archive emission, and final artifact production. Do not depend on LLVM IR, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or an LLVM-shaped backend architecture in Ultraviolet source. The Cursive bootstrap compiler may have its own bootstrap implementation details, but no LLVM dependency is allowed to carry into self-hosted Ultraviolet compiler generations.
- Windows `x86_64-win64` is the first bootstrap target. The implementation must not infer a target profile from the host platform.

## Target Repository Shape

The scaffold pass must produce the exact file tree below. Do not add compiler or runtime source files outside this tree unless this plan is updated first. The public command remains `uv`; that is a command-name exception, not a directory-name exception. Directories use PascalCase and acronym-preserving names such as `IR`, `ABI`, `COFF`, `ELF`, and `IO`.

### Exact File Tree

```text
.gitattributes
.gitignore
README.md
LICENSE.md
SPECIFICATION.md
Ultraviolet.toml
CompilerRebuildPlan.md
AGENTS.md
Compiler/
  Api.uv
  Core/
    BehaviorModel.uv  [owns behavior, undefinedness, and ill-formed rejection obligations]
    ConformanceModel.uv  [owns conformance vocabulary, constructs, checks, and recovery obligations]
    Path/
      Path.uv  [owns the public path value surface and path operation entrypoints]
      Text.uv  [owns path text ownership states and byte-view access]
      Roots.uv  [owns root predicates, root tags, tails, and absolute-path classification]
      Components.uv  [owns path segment and component extraction]
      ComponentLists.uv  [owns materialized component-list representation]
      Storage.uv  [owns path backing-storage states]
      Algebra.uv  [owns component-based path algebra]
      Resolution.uv  [owns canonical resolution and relative-path well-formedness]
    Spans.uv  [owns source-span obligations]
    SpecificationTrace.uv  [owns specification document, keyword, diagnostic-code, and reference obligations]
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
    CLI/
      Api.uv  [owns CLI host authority projection and root CLI API]
      BuildCommand.uv
      CheckCommand.uv
      CleanCommand.uv
      Commands.uv  [owns command model, command result model, dispatch vocabulary, and project.command-output obligations]
      InitCommand.uv
      Options.uv  [owns command arguments, positional arguments, target profile input, and output mode input]
      RunCommand.uv
      TestCommand.uv  [owns uv test command coordination and positional test target input routing]
      Version.uv  [owns version command display integration]
    ConformanceTrace.uv
    CrashReporting.uv
    Fingerprints.uv
    Incremental.uv
    Pipeline.uv
    TestCoverage.uv
    TestDiscovery.uv
    TestExecution.uv
    TestHarness.uv
    TestResults.uv
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
      TestAttributes.uv
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
    InstructionLowering/
      CallingSequence.uv
      FrameLayout.uv
      InstructionSelection.uv
      MachineIR.uv
      RegisterAllocation.uv
      TargetLowering.uv
      TargetMachine.uv
    ObjectFormat/
      COFFObject.uv
      ELFObject.uv
      ObjectWriter.uv
      Relocations.uv
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
  Tests/
    TestSupport/
      Assertions.uv
      CompileCase.uv
      CompileResult.uv
      Diagnostics.uv
      Sources.uv
      TestContext.uv
    Parser/
      AttributeTests.uv
      ContractTests.uv
    Checker/
      TypeTests.uv
    Lowering/
      LoweringTests.uv
    Backend/
      InstructionLoweringTests.uv
      ObjectFormatTests.uv
    Core/
      Path/
        AbsPathTests.uv
        BasenameTests.uv
        CanonTests.uv
        DropTests.uv
        FileExtTests.uv
        JoinCompTests.uv
        JoinTests.uv
        NormalizeTests.uv
        PathComponentsTests.uv
        PathFunctionTypesTests.uv
        PathPrefixTests.uv
        PathRootPredicatesTests.uv
        PathRootTagAndTailTests.uv
        PathSegmentsTests.uv
        ResolveCanonicalTests.uv
        RelativePathComputationTests.uv
        UnderTests.uv
        WFRelPathTests.uv
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
  Tests/
    Context/
      CapabilityTests.uv
    Memory/
      RegionTests.uv
    Concurrency/
      KeyTests.uv
Tools/
  Uv/
    Main.uv  [owns the uv executable source entrypoint declaration]
    Tests/
      Bootstrap/
        CursiveBootstrapTests.uv
        SelfHostRebuildTests.uv
        FixedPointTests.uv
  ExtractObligationLedger.py
Docs/
  Audit/
    FileObligationMap.csv
    MigrationLedger.md
    UltravioletObligations.csv
  Internal/
    UltravioletSpecification.obligations.md
```

`Ultraviolet.toml` must define exactly these initial assemblies:

- `UltravioletRT`: `kind = "library"`, `link_kind = "static"`, `root = "Runtime"`; produces `UltravioletRT.lib` on `x86_64-win64`.
- `UltravioletCompiler`: `kind = "library"`, `link_kind = "static"`, `root = "Compiler"`; contains all compiler logic except the CLI entrypoint.
- `uv`: `kind = "executable"`, `root = "Tools/Uv"`; this is the only lowercase assembly-name exception because it owns the public command artifact.

Only `uv` is executable so default assembly selection is unambiguous. Source-native tests live in `Tests` submodules inside their parent assembly roots: `Compiler/Tests` for `UltravioletCompiler`, `Runtime/Tests` for `UltravioletRT`, and `Tools/Uv/Tests` for `uv`.

### Runtime Naming Contract

The existing bootstrap runtime in `/mnt/c/Dev/Cursive/cursive/runtime` uses C
runtime naming at its C ABI boundary:

- CMake targets and artifacts use the existing spellings such as `cursive0_rt`
  and `CursiveRT`.
- C runtime symbols use snake_case prefixes such as `cursive_rt_*` and
  `cursive_platform_*`.
- Generated Cursive runtime symbols keep their escaped C ABI spelling, including
  `cursive_x3a_x3aruntime_*`.

The Ultraviolet runtime source tree uses the Ultraviolet style guide:

- Directories, modules, files, types, and assembly names use PascalCase with
  preserved established acronyms.
- Procedures and methods use camelCase.
- Local variables and parameters use snake_case.
- The public command assembly remains `uv`.
- The runtime library artifact spelling remains `UltravioletRT.lib` or
  `UltravioletRT.a`, as required by `def.RuntimeLibName@L5641` and
  `def.24.DefaultCallingConventionAndTargetArtifacts@L90834`.

Within Ultraviolet source, use full source names such as `Runtime`,
`RuntimeSymbol`, `RuntimeLibrary`, `StringView`, `StringManaged`, `BytesView`,
and `BytesManaged`. Use `RT` only for the spec-owned runtime library artifact
name or an external C/runtime ABI spelling.

### First Migration Object

The first migration object is the repository scaffold and `Ultraviolet.toml`. It creates the PascalCase source roots, test roots, `Docs/Audit`, `Docs/Internal`, `Docs/Audit/MigrationLedger.md`, and the initial manifest. It contains no compiler behavior migration.

Compiler source-object migration begins only after the readiness gates in `Execution Progression` are complete.

### Self-Hosted Backend Specification Reconciliation

The current `SPECIFICATION.md` and `Docs/Audit/UltravioletObligations.csv` still contain LLVM-specific backend obligations, including owners such as `backend.llvm-target`, `backend.llvm-codegen`, and LLVM-specific output/tool-resolution rules. Those obligations are retained in this plan for traceability only. They must be reconciled before backend implementation by a user-approved specification update that replaces LLVM-specific requirements with Ultraviolet-owned target-machine lowering, object emission, archive emission, and final-artifact production.

Implementation must not satisfy self-hosting by wrapping, embedding, shelling out to, or mechanically reproducing LLVM. The correct target is an Ultraviolet backend that lowers accepted semantic IR into target-specific object artifacts directly.

### Backend Model Decision

Ultraviolet must not model Rust's default backend contract, where the language compiler lowers to LLVM IR and an external backend owns target instruction selection, target-specific optimization, and object generation. That model is rejected for Ultraviolet because it would make LLVM IR the effective backend boundary and would prevent the self-hosted Ultraviolet compiler generations from being fully self-hosted.

Ultraviolet can still use explicit IR boundaries, monomorphization boundaries when required by generics, deterministic lowering traces, and pass-local conformance tests. Those are implementation practices, not permission to delegate the backend contract.

The selected model is self-owned instruction lowering:

1. `Compiler/Backend/IR` owns target-independent backend IR.
2. `Compiler/Backend/InstructionLowering` lowers backend IR to target-processor instructions represented in `MachineIR`.
3. `Compiler/Backend/ObjectFormat` encodes `MachineIR`, relocation records, symbols, and data into COFF/ELF object bytes.
4. `Compiler/Backend/Link` owns archives, final artifact production, runtime resolution, and linker behavior.

Textual assembly is not the canonical backend boundary. It may be added later only as a diagnostic listing or debugging output, and only if it is generated from the canonical `MachineIR` path.

### Module Obligation Responsibility Matrix

This matrix is the exact primary ownership map for the rebuild. For every module below, each listed obligation owner means the module is responsible for satisfying every exact obligation ID listed for that owner in Appendix A. No obligation owner from the current ledger is unassigned or assigned to more than one primary module.

### File-Level Obligation Map

`Docs/Audit/FileObligationMap.csv` is the authoritative file-level responsibility
map. It contains one row for each obligation row in
`Docs/Audit/UltravioletObligations.csv` and assigns that obligation to exactly
one source file. The map columns are `index`, `id`, `kind`, `phase`,
`strength`, `owner`, `internal_spec_line`, and `file`.

The current map covers 5,966 ledger rows, 5,943 unique obligation IDs, 165
obligation owners, and 113 owning files. Verification must reject any missing
ledger row, unknown owner, nonexistent mapped file, or obligation row with more
than one owning file.

### `Compiler/Core`

Shared compiler primitives: conformance vocabulary, behavior classes, paths,
spans, and spec tracing.

File responsibility assignments:

- `Compiler/Core/ConformanceModel.uv` owns the global conformance model,
  construct vocabulary, check-kind vocabulary, outside-conformance boundary, and
  error-recovery limits: `def.Conforming@L151`, `def.WF@L166`,
  `def.ReqJudgments@L181`, `def.TypeAndStatementNodes@L274`,
  `def.ItemKind@L289`, `def.TopDeclConstructs@L311`,
  `def.TypeCtor@L325`, `def.TypeConstructs@L366`,
  `def.PermissionConstructs@L380`, `def.ExprKind@L399`,
  `def.StmtKind@L440`, `def.ExprStmtConstructs@L467`,
  `def.CapConstructs@L481`, `def.Constructs@L495`,
  `def.OutsideConformance@L740`, `def.CheckKind@L755`,
  `def.StaticCheck@L770`, `def.RuntimeCheck@L784`,
  `def.RuntimeCheckBehavior@L798`,
  `req.ResourceExhaustionOutsideConformance@L814`,
  `def.LexRecovery@L828`, `def.ParseRecovery@L843`,
  `def.TypeRecovery@L857`, `def.MaxErrorCount@L871`,
  `def.SuggestedMaxErrorCount@L885`, and `def.AbortOnErrorCount@L899`.

- `Compiler/Core/BehaviorModel.uv` owns static judgment behavior, rule
  applicability, undefined static judgments, and ill-formed rejection:
  `Reject-IllFormed@L509`, `def.StaticJudgmentSet@L544`,
  `def.StaticRuleSet@L558`, `def.RuleShape@L572`,
  `def.JudgmentSubjectAndEnvironment@L587`,
  `def.RuleSubstitutions@L602`, `def.RuleApplies@L616`,
  `def.PremisesHold@L630`, `def.IllFormed@L644`,
  `def.StaticUndefined@L658`, `def.RuleDiagnosticIdentity@L673`,
  `def.RuleSectionIndex@L689`, `Static-Undefined@L704`, and
  `Static-Undefined-NoCode@L722`.

- `Compiler/Core/SpecificationTrace.uv` owns specification organization,
  section templates, design-contract traceability, normative keyword handling,
  diagnostic-code structure, and normative-reference traceability:
  `front-matter.document-organization@L19`,
  `front-matter.feature-section-template@L77`,
  `front-matter.language-design-contract@L102`,
  `def.NormativeKeywords@L915`, `conformance.RFC2119@L930`,
  `def.DiagnosticCodeComponents@L945`,
  `def.DiagnosticCodeFormat@L962`,
  `def.DiagnosticCodeDigitParts@L976`, `refs.NormativeRefs@L993`,
  `refs.ReferenceDetails@L1015`, and `refs.Conformance@L1036`.

- `Compiler/Core/Path/Path.uv` owns the public `Path` value surface and the
  public pure path operation entry points required by `project.path-resolution`:
  `def.PathFunctionTypes@L3268`. Accepted path result states carry validated
  records with private payloads so callers can consume canonical, relative, and
  resolved paths without constructing accepted results directly.

- `Compiler/Core/Path/Text.uv` owns path-specific text ownership states and
  byte-view access used by root classification and component discovery.

- `Compiler/Core/Path/Roots.uv` owns reusable host-path root classification,
  root tags, tails, and absoluteness required by `project.path-resolution`:
  `def.PathRootPredicates@L3146`, `def.PathRootTagAndTail@L3165`, and
  `def.AbsPath@L3253`.

- `Compiler/Core/Path/Components.uv` owns segment and component extraction
  required by `project.path-resolution`: `def.PathSegments@L3188` and
  `def.PathComponents@L3202`.

- `Compiler/Core/Path/ComponentLists.uv` owns the materialized component-list
  representation shared by `Path.uv`, `Components.uv`, `Algebra.uv`, and
  `Resolution.uv`. It is a supporting implementation file for the assigned path
  obligations and keeps `PathComps(p)` concrete for path algebra.

- `Compiler/Core/Path/Storage.uv` owns the path backing-storage modal states
  that preserve source text lifetime for component views. It is a supporting
  implementation file for the assigned path obligations.

- `Compiler/Core/Path/Algebra.uv` owns component-based path algebra required by
  `project.path-resolution`: `def.JoinComp@L3218`, `def.Join@L3237`,
  `def.PathPrefix@L3284`, `def.Normalize@L3298`, `def.Under@L3312`,
  `def.Canon@L3326`, `def.Drop@L3341`,
  `def.RelativePathComputation@L3355`, `def.Basename@L3369`, and
  `def.FileExt@L3386`. Any operation that synthesizes path backing data uses
  the built-in region surface directly: callers create `region as paths { ... }`
  and pass the active region alias to path construction and algebra procedures;
  sequence nodes are allocated with `paths ^ value`.

- `Compiler/Core/Path/Resolution.uv` owns canonical resolution and relative-path
  well-formedness required by `project.path-resolution`:
  `Resolve-Canonical@L3405`, `Resolve-Canonical-Err@L3423`,
  `WF-RelPath@L3441`, and `WF-RelPath-Err@L3459`.

- `Compiler/Core/Spans.uv` owns source-span representation and
  source-location well-formedness required by `diagnostics.source-spans`:
  `def.SourceLocation@L1283`, `def.Span@L1298`, `def.SpanRange@L1313`,
  `WF-Location@L1327`, `WF-Span@L1345`, `def.ClampSpan@L1363`, and
  `Span-Of@L1380`.

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

Compilation pipeline ordering, conformance traces, incremental fingerprints, crash reporting, source-native test discovery and execution, and test coverage reporting.

`Compiler/Driver/CLI` owns command parsing, command selection, command-specific entrypoints, command options, and version display. `Compiler/Driver/CLI/TestCommand.uv` owns CLI integration for `uv test`. `TestDiscovery.uv` owns deterministic discovery of `[[test]]` procedures. `TestHarness.uv` owns generated harness construction. `TestExecution.uv` owns invocation and result classification. `TestResults.uv` owns result records and rendering. `TestCoverage.uv` owns `covers(...)` extraction and obligation-ledger coverage checks.

Executable command surface responsibilities:

- `Tools/Uv/Main.uv` owns the `uv` executable source entrypoint. It must define
  exactly one `public procedure main(context: Context) -> i32`, delegate to the
  driver CLI, and provide the source declaration consumed by
  `def.15.MainEntryPointDefinitions@L52786` and `rule.15.Main-Ok@L52807`.
  The main-check implementation for those obligations is owned by
  `Compiler/Semantics/Declarations/Procedures.uv`.
- `Compiler/Driver/CLI/Api.uv` owns `DriverHost`, the CLI authority projection
  created from the `Context` received by `main`.
- `Compiler/Driver/CLI/Options.uv` owns command argument records, positional
  argument structure, target-profile/output-mode option input, and the
  `uv test [target]` target text shape consumed by
  `lowering.TestHarnessGeneration@L28498`.
- `Compiler/Driver/CLI/Commands.uv` owns the command modal, command result
  modal, dispatch vocabulary, and command output obligations:
  `def.DumpProjectOutput@L2073`, `def.ProjectSummaryOutput@L2090`,
  `def.OutputSummary@L2104`, `def.LinkOutputSummary@L2118`,
  `def.IROpt@L2134`, and `def.ImportLibOpt@L2150`.
- `Compiler/Driver/CLI/TestCommand.uv` owns `uv test` command coordination for
  the positional target model in `lowering.TestHarnessGeneration@L28498`. It
  routes target resolution to `TestHarness.uv`, deterministic discovery to
  `TestDiscovery.uv`, coverage validation to `TestCoverage.uv`, execution to
  `TestExecution.uv`, and result reporting to `TestResults.uv`.
- `Compiler/Driver/CLI/Version.uv` owns version command display text and its
  command-result integration.

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

Attribute target validation and static semantics for layout, metadata, diagnostics, optimization, and `[[test]]` attributes.

`Compiler/Semantics/Attributes/TestAttributes.uv` owns `[[test]]` target checking, argument validation, test-procedure shape checks, and test-attribute diagnostics after the user-approved specification update.
Attribute target validation and static semantics for layout, metadata, diagnostics, optimization, and `[[test]]` attributes.

`Compiler/Semantics/Attributes/TestAttributes.uv` owns `[[test]]` target checking, argument validation, test-procedure shape checks, and test-attribute diagnostics after the user-approved specification update.

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

Provenance, region checks, safe pointer checks, initialization state, and drop-state facts.
Provenance, region checks, safe pointer checks, initialization state, and drop-state facts.

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

Backend-level ownership for target-independent backend rules, backend diagnostics, and final backend pipeline coordination. Current `spec.backend` obligations are LLVM-shaped in the ledger and must be reconciled in the specification before implementation; the Ultraviolet implementation target is a self-hosted backend, not LLVM.
Backend-level ownership for target-independent backend rules, backend diagnostics, and final backend pipeline coordination. Current `spec.backend` obligations are LLVM-shaped in the ledger and must be reconciled in the specification before implementation; the Ultraviolet implementation target is a self-hosted backend, not LLVM.

Obligation owners:
- `spec.backend` (190 total, 190 required)

### `Compiler/Backend/IR`

IR model, global/static symbols, runtime symbols, literal data, vtables, poison state, and IR dump/build helpers.

Obligation owners:
- `codegen` (51 total, 51 required)
- `spec.runtime-interface` (64 total, 64 required)
- `spec.symbols` (51 total, 51 required)

### `Compiler/Backend/InstructionLowering`
### `Compiler/Backend/InstructionLowering`

Lowering backend IR into target-processor instructions in `MachineIR`, including target-machine description, instruction selection, register allocation, frame layout, calling sequence construction, and target legalization. This module does not emit textual assembly or object bytes. The current ledger owner name `backend.llvm-target` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.
Lowering backend IR into target-processor instructions in `MachineIR`, including target-machine description, instruction selection, register allocation, frame layout, calling sequence construction, and target legalization. This module does not emit textual assembly or object bytes. The current ledger owner name `backend.llvm-target` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-target` (3 total, 3 required)

### `Compiler/Backend/ObjectFormat`

Relocation generation and COFF/ELF object writing. The current ledger owner name `backend.llvm-codegen` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-codegen` (4 total, 4 required)

### `Compiler/Backend/ObjectFormat`

Relocation generation and COFF/ELF object writing. The current ledger owner name `backend.llvm-codegen` is retained here only as a traceability label until the specification and obligation ledger are updated to self-hosted backend terminology.

Obligation owners:
- `backend.llvm-codegen` (4 total, 4 required)

### `Compiler/Backend/Link`

Output pipeline, linker/archive plans, runtime library resolution, external library materialization, and artifact finalization. This module must emit final artifacts without relying on LLVM linker or archiver tools.
Output pipeline, linker/archive plans, runtime library resolution, external library materialization, and artifact finalization. This module must emit final artifacts without relying on LLVM linker or archiver tools.

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

Cursive compiler path for the current workstation: `C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe`.
Cursive compiler path for the current workstation: `C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe`.

Required Cursive commands use Windows target selection explicitly:
Required Cursive commands use Windows target selection explicitly:

```sh
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 --check C:\Dev\Ultraviolet
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletRT
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletCompiler
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly uv
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 --check C:\Dev\Ultraviolet
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletRT
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly UltravioletCompiler
"C:\Dev\Cursive\cursive\build\windows\Release\Cursive.exe" --target-profile x86_64-win64 build C:\Dev\Ultraviolet --assembly uv
```

The self-host ladder is:

1. Cursive bootstrap compiler: Cursive compiles `UltravioletRT`, `UltravioletCompiler`, and `uv` from Ultraviolet source. Any Cursive-internal backend dependency is bootstrap machinery only.
2. First self-hosted compiler: the bootstrap-built `uv.exe` compiles the same assemblies using Ultraviolet's target-instruction lowering and artifact emission.
3. Fixed-point compiler: the first self-hosted `uv.exe` compiles the same assemblies again using Ultraviolet's target-instruction lowering and artifact emission.
4. Completion requires matching diagnostics, matching conformance traces, stable normalized IR, and stable artifact fingerprints between the two self-hosted compiler outputs.
1. Cursive bootstrap compiler: Cursive compiles `UltravioletRT`, `UltravioletCompiler`, and `uv` from Ultraviolet source. Any Cursive-internal backend dependency is bootstrap machinery only.
2. First self-hosted compiler: the bootstrap-built `uv.exe` compiles the same assemblies using Ultraviolet's target-instruction lowering and artifact emission.
3. Fixed-point compiler: the first self-hosted `uv.exe` compiles the same assemblies again using Ultraviolet's target-instruction lowering and artifact emission.
4. Completion requires matching diagnostics, matching conformance traces, stable normalized IR, and stable artifact fingerprints between the two self-hosted compiler outputs.

## Per-File Migration Protocol

Each migrated source object must go through this exact gate before it is accepted:

1. Identify the C++ source object(s), current tests, and current call sites.
2. Identify the canonical target Ultraviolet module and file; use the planned semantic module structure instead of the legacy numbered C++ translation-pass layout.
2. Identify the canonical target Ultraviolet module and file; use the planned semantic module structure instead of the legacy numbered C++ translation-pass layout.
3. Map the object to obligation owners and exact obligation IDs from Appendix A.
4. Read the corresponding `SPECIFICATION.md` section before writing code.
5. Implement the general rule in the canonical module; delete or avoid any duplicate behavior path.
6. Add source-native conformance tests for valid examples, invalid examples, diagnostics, phase ordering, and relevant target-profile behavior.
7. Run the Cursive `--check` command for the affected assembly and the targeted conformance test set.
6. Add source-native conformance tests for valid examples, invalid examples, diagnostics, phase ordering, and relevant target-profile behavior.
7. Run the Cursive `--check` command for the affected assembly and the targeted conformance test set.
8. Record in `Docs/Audit/MigrationLedger.md`: source object, target file, obligation IDs, tests, command output summary, and any C++ bootstrap bug found.

A file is not migrated if it merely compiles. It is migrated only when its mapped required obligations are covered by tests or an explicit, reviewed non-applicability note.

### Migration Ledger Entry Format

Every accepted migration entry in `Docs/Audit/MigrationLedger.md` must use this format:

- Source object: original C++ path, primary symbols, and current call sites.
- Target object: Ultraviolet module path, file path, and owned responsibility.
- Specification basis: exact `SPECIFICATION.md` sections read and obligation IDs from Appendix A.
- Implementation summary: canonical behavior implemented and old behavior reconciled.
- Tests: source-native test files, `[[test]]` names, covered obligation IDs, and diagnostic cases.
- Verification commands: exact command lines, target profile, assembly, and summarized output.
- Bootstrap notes: Cursive bootstrap issue found, patch reference, or `none`.
- Non-applicability notes: obligation ID, reason, spec citation, and reviewer approval.
- Acceptance: reviewer, date, and status.

## Execution Progression

The rebuild proceeds by compiler dependency order, not by the historical C++ file order. A later progression step may start only after the earlier step exposes a stable checked surface and source-native conformance tests for the obligations it owns.

1. **Scaffold and manifest**: create the clean PascalCase repository tree, `Ultraviolet.toml`, assembly roots, Docs/Audit locations, and bootstrap manifest shape. Do not add compiler behavior in this step.
2. **Cursive Ultraviolet project support**: lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows `x86_64-win64` target profile, runtime library naming, tool resolution behavior, and no-host-inference policy.
3. **Source-native conformance testing**: apply the user-approved specification update for `[[test]]`, contract-defined test success, `covers(...)` obligation metadata, and `uv test` discovery under each parent assembly's `Tests` submodule.
4. **Foundation**: implement source positions, spans, identifiers, paths, deterministic ordering primitives, diagnostic records, diagnostic rendering, and obligation trace plumbing.
5. **Project model**: implement manifest parsing, root discovery, assembly loading, assembly graph construction, module discovery, source root rules, output planning, and explicit target profile selection.
6. **Source text and lexer**: implement UTF-8/BOM/newline handling, Unicode identifier rules, Unicode security checks, comments, whitespace, literals, operators, statement termination, maximal munch, and token records.
7. **Parser and AST**: implement parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, file parsing, and file-to-module aggregation.
8. **Name resolution and visibility**: implement module path validation, name introduction, duplicate detection, scope lookup, qualified lookup, imports, `using` forms, accessibility, and stable symbol identity.
9. **Core type and permission semantics**: implement type well-formedness, equivalence, subtyping rules, inference, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.
10. **Declarations and type constructs**: implement records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, procedures, methods, contracts, refinements, and capability classes.
11. **Executable semantics**: implement statements, expressions, patterns, key acquisition/release/conflict behavior, structured parallelism, async state machines, compile-time forms, propagation, control flow, and phase ordering.
12. **Runtime interface and lifecycle**: implement authority roots, capability attenuation, host/runtime primitives, foreign ABI validation, hosted exports, module/static initialization, cleanup, unwind/drop hooks, and runtime symbol naming.
13. **Canonical lowering**: lower accepted semantic forms into target-independent backend IR. This step must preserve checked language semantics and must not select target processor instructions.
14. **Self-owned backend**: implement backend IR, ABI lowering, `InstructionLowering`, `MachineIR`, `ObjectFormat`, and `Link` in that order. `InstructionLowering` lowers to target-processor instructions in internal `MachineIR`; `ObjectFormat` encodes COFF/ELF object bytes; `Link` owns archives and final artifacts.
15. **Driver**: implement the public `uv` command, check/build modes, assembly selection, diagnostics output, artifact layout, target selection, and build failure behavior.
16. **Self-host ladder**: the Cursive bootstrap compiler builds `UltravioletRT`, `UltravioletCompiler`, and `uv`; the first self-hosted `uv.exe` rebuilds them; the fixed-point compiler rebuilds them again. Completion requires stable diagnostics, conformance traces, normalized IR, and artifact fingerprints between the self-hosted compiler outputs.

Each progression step is accepted only when every migrated object in that step has a `Docs/Audit/MigrationLedger.md` entry, mapped obligation IDs, source-native conformance tests, and fresh command output. If a step exposes a Cursive bootstrap defect, patch Cursive at the root cause before continuing the Ultraviolet rebuild.

Compiler source-object migration begins only after the following readiness gates are complete:

- the repository scaffold and manifest exist in the PascalCase target shape;
- the `[[test]]` specification update is approved;
- the obligation ledger is regenerated from the approved specification;
- the Cursive bootstrap compiler accepts the `[[test]]` surface required for bootstrap testing;
- `uv test` ownership and harness behavior are implemented far enough to run at least one source-native compiler conformance test;
- `Docs/Audit/MigrationLedger.md` defines the required entry format for per-object migration evidence.

Before these gates are complete, allowed work is scaffold, manifest, specification reconciliation, obligation ledger regeneration, Cursive Ultraviolet project support, and source-native test-surface implementation.

### Migration Ledger Entry Format

Every accepted migration entry in `Docs/Audit/MigrationLedger.md` must use this format:

- Source object: original C++ path, primary symbols, and current call sites.
- Target object: Ultraviolet module path, file path, and owned responsibility.
- Specification basis: exact `SPECIFICATION.md` sections read and obligation IDs from Appendix A.
- Implementation summary: canonical behavior implemented and old behavior reconciled.
- Tests: source-native test files, `[[test]]` names, covered obligation IDs, and diagnostic cases.
- Verification commands: exact command lines, target profile, assembly, and summarized output.
- Bootstrap notes: Cursive bootstrap issue found, patch reference, or `none`.
- Non-applicability notes: obligation ID, reason, spec citation, and reviewer approval.
- Acceptance: reviewer, date, and status.

## Execution Progression

The rebuild proceeds by compiler dependency order, not by the historical C++ file order. A later progression step may start only after the earlier step exposes a stable checked surface and source-native conformance tests for the obligations it owns.

1. **Scaffold and manifest**: create the clean PascalCase repository tree, `Ultraviolet.toml`, assembly roots, Docs/Audit locations, and bootstrap manifest shape. Do not add compiler behavior in this step.
2. **Cursive Ultraviolet project support**: lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows `x86_64-win64` target profile, runtime library naming, tool resolution behavior, and no-host-inference policy.
3. **Source-native conformance testing**: apply the user-approved specification update for `[[test]]`, contract-defined test success, `covers(...)` obligation metadata, and `uv test` discovery under each parent assembly's `Tests` submodule.
4. **Foundation**: implement source positions, spans, identifiers, paths, deterministic ordering primitives, diagnostic records, diagnostic rendering, and obligation trace plumbing.
5. **Project model**: implement manifest parsing, root discovery, assembly loading, assembly graph construction, module discovery, source root rules, output planning, and explicit target profile selection.
6. **Source text and lexer**: implement UTF-8/BOM/newline handling, Unicode identifier rules, Unicode security checks, comments, whitespace, literals, operators, statement termination, maximal munch, and token records.
7. **Parser and AST**: implement parser state, AST forms, item sequencing, terminators, doc association, recovery, attribute syntax, extern block parsing, file parsing, and file-to-module aggregation.
8. **Name resolution and visibility**: implement module path validation, name introduction, duplicate detection, scope lookup, qualified lookup, imports, `using` forms, accessibility, and stable symbol identity.
9. **Core type and permission semantics**: implement type well-formedness, equivalence, subtyping rules, inference, permission admissibility, binding activity, provenance, regions, and FFI-safety checks.
10. **Declarations and type constructs**: implement records, enums, unions, aliases, modal types, strings, bytes, pointers, functions, closures, generics, classes, implementations, associated types, dynamic class objects, procedures, methods, contracts, refinements, and capability classes.
11. **Executable semantics**: implement statements, expressions, patterns, key acquisition/release/conflict behavior, structured parallelism, async state machines, compile-time forms, propagation, control flow, and phase ordering.
12. **Runtime interface and lifecycle**: implement authority roots, capability attenuation, host/runtime primitives, foreign ABI validation, hosted exports, module/static initialization, cleanup, unwind/drop hooks, and runtime symbol naming.
13. **Canonical lowering**: lower accepted semantic forms into target-independent backend IR. This step must preserve checked language semantics and must not select target processor instructions.
14. **Self-owned backend**: implement backend IR, ABI lowering, `InstructionLowering`, `MachineIR`, `ObjectFormat`, and `Link` in that order. `InstructionLowering` lowers to target-processor instructions in internal `MachineIR`; `ObjectFormat` encodes COFF/ELF object bytes; `Link` owns archives and final artifacts.
15. **Driver**: implement the public `uv` command, check/build modes, assembly selection, diagnostics output, artifact layout, target selection, and build failure behavior.
16. **Self-host ladder**: the Cursive bootstrap compiler builds `UltravioletRT`, `UltravioletCompiler`, and `uv`; the first self-hosted `uv.exe` rebuilds them; the fixed-point compiler rebuilds them again. Completion requires stable diagnostics, conformance traces, normalized IR, and artifact fingerprints between the self-hosted compiler outputs.

Each progression step is accepted only when every migrated object in that step has a `Docs/Audit/MigrationLedger.md` entry, mapped obligation IDs, source-native conformance tests, and fresh command output. If a step exposes a Cursive bootstrap defect, patch Cursive at the root cause before continuing the Ultraviolet rebuild.

Compiler source-object migration begins only after the following readiness gates are complete:

- the repository scaffold and manifest exist in the PascalCase target shape;
- the `[[test]]` specification update is approved;
- the obligation ledger is regenerated from the approved specification;
- the Cursive bootstrap compiler accepts the `[[test]]` surface required for bootstrap testing;
- `uv test` ownership and harness behavior are implemented far enough to run at least one source-native compiler conformance test;
- `Docs/Audit/MigrationLedger.md` defines the required entry format for per-object migration evidence.

Before these gates are complete, allowed work is scaffold, manifest, specification reconciliation, obligation ledger regeneration, Cursive Ultraviolet project support, and source-native test-surface implementation.

## Workstreams And Required Obligation Owners

Each workstream lists the exact obligation owners it must discharge. Appendix A lists the exact obligation IDs under each owner.

### Repository Scaffold And Style Normalization

Create the final PascalCase repository structure, normalize existing lowercase Docs/Tools material, and introduce the bootstrap manifest without compiler logic.
Create the final PascalCase repository structure, normalize existing lowercase Docs/Tools material, and introduce the bootstrap manifest without compiler logic.

Obligation owners:
- `front-matter.language-design-contract` (1 total, 0 required, 1 recommended)
- `conformance.document-conventions` (5 total, 5 required)
- `project.core-records` (3 total, 3 required)
- `project.manifest-schema` (12 total, 12 required)
- `project.manifest-validation` (31 total, 31 required)
- `project.source-roots` (3 total, 3 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### Cursive Ultraviolet Project Support
### Cursive Ultraviolet Project Support

Lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows target profile, runtime library name, tool resolution, and no-host-inference policy.
Lock normal Cursive project detection for `Ultraviolet.toml` and `.uv` files, Windows target profile, runtime library name, tool resolution, and no-host-inference policy.

Obligation owners:
- `conformance.translation-phases` (6 total, 6 required)
- `conformance.phase-ordering` (8 total, 8 required)
- `conformance.target-abi` (7 total, 7 required)
- `project.toolchain-config` (6 total, 6 required)
- `project.tool-resolution` (7 total, 7 required)
- `project.output-artifacts` (43 total, 43 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### Foundation And Diagnostics

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

### Project Model And Output Planning

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

### Source Text And Lexer

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

### Parser, AST, Attributes, And Module Aggregation

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

### Authority And Runtime Model

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

### Modules, Name Resolution, And Visibility

Rebuild module path validation, name introduction, duplicate detection, scope lookup, qualified resolution, imports, using forms, and accessibility.

Obligation owners:
- `checker.modules` (209 total, 209 required)
- `checker.name-resolution` (307 total, 307 required)
- `checker.visibility` (15 total, 15 required)
- `diagnostics.name-resolution` (1 total, 1 required)

Acceptance gate: all required obligation IDs for the owners above are either implemented and tested or recorded as not-applicable with a spec citation and reviewer approval.

### Type System, Permissions, And Core Static Semantics

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

### Declarations And Type Constructs

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

### Statements, Expressions, Patterns, Keys, Parallelism, Async, And Comptime

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

### FFI, Initialization, Runtime Interface, Cleanup, And Symbols

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

### Backend Specification Reconciliation Gate

Backend migration begins only after the LLVM-shaped specification obligations are replaced by user-approved self-hosted backend obligations and the obligation ledger is regenerated. The reconciliation must replace LLVM-owner terminology with owner names for target-machine description, backend IR, ABI lowering, instruction lowering, object-format emission, archive emission, and final artifact production.

#### Backend Specification Approval Packet

Before W12, submit an approval request that includes:

- exact replacement text for LLVM-shaped backend sections;
- regenerated obligation-owner names and counts;
- mapping from old LLVM-named obligations to self-hosted backend obligations;
- definitions for backend IR, `MachineIR`, target-machine constants, ABI lowering, instruction lowering, object writing, archive writing, linking, and artifact fingerprinting;
- conformance tests for backend IR, instruction lowering, object bytes, archive output, linker output, and self-host fixed-point stability.

Minimum owner replacements:

- `backend.llvm-target` -> `backend.target-machine`
- `backend.llvm-codegen` -> `backend.instruction-lowering` and `backend.object-format`
- LLVM IR emission and LLVM tool-resolution obligations -> backend IR, `MachineIR`, object writer, archive writer, link, and finalization obligations

W12 acceptance uses the regenerated self-hosted backend obligations. Legacy LLVM-named rows may remain only in historical audit material.

### Backend Specification Reconciliation Gate

Backend migration begins only after the LLVM-shaped specification obligations are replaced by user-approved self-hosted backend obligations and the obligation ledger is regenerated. The reconciliation must replace LLVM-owner terminology with owner names for target-machine description, backend IR, ABI lowering, instruction lowering, object-format emission, archive emission, and final artifact production.

#### Backend Specification Approval Packet

Before W12, submit an approval request that includes:

- exact replacement text for LLVM-shaped backend sections;
- regenerated obligation-owner names and counts;
- mapping from old LLVM-named obligations to self-hosted backend obligations;
- definitions for backend IR, `MachineIR`, target-machine constants, ABI lowering, instruction lowering, object writing, archive writing, linking, and artifact fingerprinting;
- conformance tests for backend IR, instruction lowering, object bytes, archive output, linker output, and self-host fixed-point stability.

Minimum owner replacements:

- `backend.llvm-target` -> `backend.target-machine`
- `backend.llvm-codegen` -> `backend.instruction-lowering` and `backend.object-format`
- LLVM IR emission and LLVM tool-resolution obligations -> backend IR, `MachineIR`, object writer, archive writer, link, and finalization obligations

W12 acceptance uses the regenerated self-hosted backend obligations. Legacy LLVM-named rows may remain only in historical audit material.

### Lowering, Backend, Driver, And Self-Host

Rebuild canonical lowering, IR, target-machine constants, ABI lowering, instruction lowering, object emission, linking, archiving, driver commands, and bootstrap-to-fixed-point self-host verification. Current LLVM-named obligation owners remain in the ledger for traceability, but implementation must satisfy the corrected self-hosted backend specification rather than LLVM behavior.
Rebuild canonical lowering, IR, target-machine constants, ABI lowering, instruction lowering, object emission, linking, archiving, driver commands, and bootstrap-to-fixed-point self-host verification. Current LLVM-named obligation owners remain in the ledger for traceability, but implementation must satisfy the corrected self-hosted backend specification rather than LLVM behavior.

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

## Source-Native Conformance Testing

The rebuild uses a specification-owned `[[test]]` procedure attribute, ordinary
procedure contracts as the test oracle, and optional `covers(...)` metadata for
audit coverage. The specification update is recorded in §9.6 and the obligation
ledger has been regenerated.

Specification obligations directly affected by the update:

- Attributes: `def.SpecAttributeRegistry@L26939`, `def.SpecAttributeTargets@L26962`, `def.AttributeStaticSemantics.Helpers2@L27066`.
- Test attributes: `grammar.TestAttribute@L28318`, `parse.TestAttributeByOrdinaryAttributeParser@L28338`, `ast.TestProcedureClassification@L28356`, `def.TestName@L28371`, `def.TestCoverage@L28386`, `req.TestAttributeProcedureTarget@L28403`, `def.TestAttributeArgsOk@L28417`, `req.TestProcedureShape@L28437`, `req.TestContextAuthority@L28459`, `conformance.TestAttributeDynamicSemantics@L28477`, `lowering.TestHarnessGeneration@L28498`, `def.TestDiscoveryOrder@L28557`, `diagnostics.TestAttributes@L28575`.
- Contracts: `grammar.15.Postconditions@L55203`, `def.15.PostconditionProofContext@L55285`, `rule.15.Post-Valid@L55300`, `req.15.ContractResultProperties@L55336`, `req.15.PostconditionResultRuntimeBinding@L55501`.
- Project model and parent assembly loading: `WF-Assembly-Kind@L2394`, `WF-Assembly@L3511`, `Select-By-Name@L3887`.

Compiler conformance tests must live in PascalCase `Tests` submodules inside the parent assembly root that owns the behavior. Test helper code lives in `Compiler/Tests/TestSupport`; parser tests live in `Compiler/Tests/Parser`; checker tests live in `Compiler/Tests/Checker`; lowering and backend tests live in `Compiler/Tests/Lowering` and `Compiler/Tests/Backend`; runtime tests live in `Runtime/Tests`; bootstrap fixed-point tests live in `Tools/Uv/Tests/Bootstrap`.

`Compiler/Tests/TestSupport/TestContext.uv` owns the compiler-rebuild test context type. The generated harness imports `UltravioletCompiler::Tests::TestSupport` and injects `TestContext` only for tests whose single parameter has that exact type. `TestContext` is a compiler test-support API for this rebuild. Parameterless `[[test]]` procedures remain valid. General user-facing test support beyond this rebuild requires a separate approved public tooling/library design.

`uv test` uses a positional target model:

```text
uv test
uv test UltravioletCompiler
uv test UltravioletCompiler::Tests::Parser
uv test Compiler/Tests/Parser/AttributeTests.uv
uv test C:\Dev\Ultraviolet
uv test C:\Dev\Ultraviolet\Compiler\Tests\Parser\AttributeTests.uv
```

Target resolution:

1. No target: discover and run every assembly that owns a `Tests` module subtree.
2. Assembly name: run that assembly's `Tests` module subtree.
3. Module path: run tests under that module subtree.
4. Source file path: run tests in that source file.
5. Directory path: find the nearest `Ultraviolet.toml`; if the directory is the project root, run all test-bearing assemblies, and if it is inside an assembly root, run tests under that directory.

Execution model:

1. Resolve the positional target into one or more test-bearing assembly scopes.
2. Load each selected assembly with ordinary project loading.
3. Discover `[[test]]` procedures under the resolved `Tests` scope.
4. Build an ephemeral test harness module under the selected assembly's build output directory.
5. Compile the selected assembly with the harness entrypoint for test execution.
6. Invoke discovered tests in deterministic order.
7. Report results and coverage through the driver.

The generated harness is build output, not source-controlled project content. Discovery order is module path, file order, declaration span, then fully-qualified procedure symbol. Each discovered test receives a stable identity from its fully-qualified procedure path; `name: "..."` is a display label.

Test result categories:

- `Passed`: the procedure returns normally and its postcondition is satisfied.
- `Failed`: the procedure returns normally and its postcondition is violated.
- `Error`: the test procedure is ill-formed, precondition evaluation fails, execution panics, the compiler crashes, harness generation fails, or required test authority cannot be provided.

Example test shape:

```uv
[[test(
    name: "unknown attribute is rejected",
    covers("AttrList-Unknown@L27030"),
    covers("diagnostics.AttributeDiagnostics@L27171")
)]]
internal procedure unknownAttributeIsRejected(context: TestContext) -> CompileResult
|: =>
    rejected(@result)
    && hasDiagnostic(@result, DiagnosticCode.E_MOD_2451)
{
    let source: SourceText = SourceText.fromLines([
        "[[unknown]]",
        "internal procedure sample() -> bool",
        "|: => @result",
        "{",
        "    return true",
        "}"
    ])

    return compileSource(context, source)
}
```

The `covers(...)` metadata is required for compiler audit tests and optional for ordinary user tests. Source text embedded in tests is the canonical representation for parser, checker, diagnostic, phase-ordering, lowering, backend, runtime, and bootstrap assertions. External test data is reserved for byte-level object, archive, linker, and fixed-point artifact comparisons where inline source cannot represent the checked artifact. Unknown `covers(...)` references fail audit coverage checking.


## Completion Criteria

The rebuild is complete only when all of the following are true:

- The repository tree is PascalCase except externally mandated source, ABI, serialized, or command names.
- `Docs/Audit/MigrationLedger.md` contains one accepted entry for every migrated source object and every required obligation owner in Appendix A.
- The Cursive bootstrap compiler compiles the full Ultraviolet runtime, compiler library, and `uv` executable on `x86_64-win64`.
- The first self-hosted `uv.exe` recompiles itself and the runtime.
- The fixed-point compiler output is stable against the first self-hosted output by normalized diagnostics, conformance traces, IR, and artifact fingerprints.
- The self-hosted compiler generations do not invoke LLVM IR generation, LLVM libraries, `llvm-as`, `lld-link`, `llvm-lib`, or any LLVM-compatible compatibility layer.
- No reachable duplicate compiler implementation, compatibility shim, fallback path, temporary branch, or test-only path remains.
- Every required obligation ID in Appendix A is implemented, tested, or explicitly classified as outside the compiler/runtime implementation boundary with a spec citation.

## Appendix A - Exact Obligation References By Construct Owner

Reference format: `obligation-id@Linternal_spec_line`. The line number is the `internal_spec_line` from the current obligation ledger, not a Markdown line in this plan. The complete source ledger currently contains 5967 obligations.

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

- `diagnostics.RuntimeStateAndMemoryDiagnostics@L18420`

#### `diagnostics.name-resolution`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L23703-L23703.

- `diagnostics.NameResolutionAndReservedNames@L23757`

#### `diagnostics.types`

Count: 2 total; 1 required; 0 recommended; 0 informative. Ledger line span: L26206-L40949.

- `diagnostics.CoreTypeDiagnostics@L26260`, `diagnostics.DataTypesSupplement@L41290`
- `diagnostics.CoreTypeDiagnostics@L26260`, `diagnostics.DataTypesSupplement@L41290`

#### `diagnostics.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27115-L27324.

- `diagnostics.AttributeDiagnostics@L27171`, `diagnostics.VendorAttributeDiagnostics@L27380`
- `diagnostics.AttributeDiagnostics@L27171`, `diagnostics.VendorAttributeDiagnostics@L27380`

#### `diagnostics.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27611-L27611.

- `diagnostics.LayoutAttributeDiagnostics@L27667`
- `diagnostics.LayoutAttributeDiagnostics@L27667`

#### `diagnostics.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27756-L27756.

- `diagnostics.OptimizationAttributeDiagnostics@L27812`
- `diagnostics.OptimizationAttributeDiagnostics@L27812`

#### `diagnostics.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28237-L28237.

- `diagnostics.DiagnosticsMetadataAttributes@L28293`
- `diagnostics.DiagnosticsMetadataAttributes@L28293`

#### `diagnostics.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28549-L28702.

- `req.PermissionFormsDiagnosticOwnership@L28890`, `req.AliasExclusivityDiagnosticOwnership@L29043`
- `req.PermissionFormsDiagnosticOwnership@L28890`, `req.AliasExclusivityDiagnosticOwnership@L29043`

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

- `grammar.AttributeSyntaxAndPlacement@L26288`, `req.AttributeReservedLeafNames@L26319`, `req.AttributeImmediateTargetPlacement@L26333`, `Parse-AttrListOpt-None@L26349`, `Parse-AttrListOpt-Yes@L26367`, `Parse-AttrList-Cons@L26385`, `Parse-AttrListTail-End@L26403`, `Parse-AttrListTail-Cons@L26421`
- `Parse-AttrBlock@L26439`, `Parse-AttrSpecList-Cons@L26457`, `Parse-AttrSpecListTail-End@L26475`, `Parse-AttrSpecListTail-TrailingComma@L26493`, `Parse-AttrSpecListTail-Comma@L26511`, `Parse-AttrSpec@L26529`, `Parse-AttrArgsOpt-None@L26547`, `Parse-AttrArgsOpt-Yes@L26565`
- `Parse-AttrArgList-Cons@L26583`, `Parse-AttrArgListTail-End@L26601`, `Parse-AttrArgListTail-TrailingComma@L26619`, `Parse-AttrArgListTail-Comma@L26637`, `Parse-AttrArg-Named-Literal@L26655`, `Parse-AttrArg-Named-Ident@L26673`, `Parse-AttrArg-Named-Call@L26691`, `Parse-AttrArg-Literal@L26709`
- `Parse-AttrArg-Ident@L26727`, `def.AttributeAstRepresentation@L26748`, `def.AttributeVendorPrefixAst@L26767`, `def.AttributeArgumentAst@L26781`, `def.AttributeSpecAst@L26795`, `def.AttributeListAst@L26809`, `def.ExpressionAttributes@L26824`, `def.AttachExpressionAttributes@L26838`
- `def.ItemAttributeList@L26852`, `def.AttributeByName@L26867`, `conformance.VendorAttributeSyntaxReuse@L27191`, `req.VendorAttributeParserReuse@L27209`, `def.AttributeLeafToken@L27223`, `Parse-AttrName-Plain@L27237`, `Parse-AttrName-Vendor@L27255`, `Parse-VendorPrefixTail-End@L27273`
- `Parse-VendorPrefixTail-Cons@L27291`, `def.VendorAttributeAst@L27312`
- `def.ItemAttributeList@L26852`, `def.AttributeByName@L26867`, `conformance.VendorAttributeSyntaxReuse@L27191`, `req.VendorAttributeParserReuse@L27209`, `def.AttributeLeafToken@L27223`, `Parse-AttrName-Plain@L27237`, `Parse-AttrName-Vendor@L27255`, `Parse-VendorPrefixTail-End@L27273`
- `Parse-VendorPrefixTail-Cons@L27291`, `def.VendorAttributeAst@L27312`

#### `parser.attributes.layout`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27344-L27383.

- `grammar.LayoutAttributeSyntax@L27400`, `req.LayoutAttributeParserReuse@L27423`, `def.LayoutAttributeAstAttachment@L27439`
- `grammar.LayoutAttributeSyntax@L27400`, `req.LayoutAttributeParserReuse@L27423`, `def.LayoutAttributeAstAttachment@L27439`

#### `parser.attributes.optimization`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27633-L27672.

- `grammar.OptimizationAttributeSyntax@L27689`, `req.OptimizationAttributeParserReuse@L27712`, `def.OptimizationAttributeAstAttachment@L27728`
- `grammar.OptimizationAttributeSyntax@L27689`, `req.OptimizationAttributeParserReuse@L27712`, `def.OptimizationAttributeAstAttachment@L27728`

#### `parser.attributes.metadata`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L27774-L27823.

- `req.DiagnosticsMetadataSyntaxParsingAst@L27830`, `req.DiagnosticsMetadataParserReuse@L27848`, `def.ExpressionAttributeList@L27864`, `def.ExpressionAttributeByName@L27879`
- `req.DiagnosticsMetadataSyntaxParsingAst@L27830`, `req.DiagnosticsMetadataParserReuse@L27848`, `def.ExpressionAttributeList@L27864`, `def.ExpressionAttributeByName@L27879`

#### `parser.permissions`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L28262-L28752.

- `grammar.PermissionFormsSyntax@L28603`, `req.PermissionQualifierTypeGrammarPlacement@L28622`, `req.PermissionParserOwnership@L28638`, `req.ParseReceiverCanonicalOwner@L28652`, `def.PermissionAstForms@L28668`, `def.PermissionQualifiedTypeAst@L28686`, `req.AliasExclusivityNoParsingRules@L28925`, `req.AliasExclusivityNoAstForms@L28941`
- `req.BindingActivityNoParsingRules@L29077`, `req.BindingActivityNoAstNode@L29093`
- `grammar.PermissionFormsSyntax@L28603`, `req.PermissionQualifierTypeGrammarPlacement@L28622`, `req.PermissionParserOwnership@L28638`, `req.ParseReceiverCanonicalOwner@L28652`, `def.PermissionAstForms@L28668`, `def.PermissionQualifiedTypeAst@L28686`, `req.AliasExclusivityNoParsingRules@L28925`, `req.AliasExclusivityNoAstForms@L28941`
- `req.BindingActivityNoParsingRules@L29077`, `req.BindingActivityNoAstNode@L29093`

#### `parser`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L28918-L30777.

- `req.PermissionAdmissibilityNoAdditionalParsing@L29259`, `grammar.ImportDeclarationSyntax@L29496`, `req.ImportDeclarationParserBranch@L29516`, `grammar.UsingDeclarationSyntax@L29740`, `grammar.StaticDeclarationSyntax@L30250`, `req.StaticDeclParserOwnership@L30283`, `grammar.ExternBlockShellSyntax@L31118`
- `req.PermissionAdmissibilityNoAdditionalParsing@L29259`, `grammar.ImportDeclarationSyntax@L29496`, `req.ImportDeclarationParserBranch@L29516`, `grammar.UsingDeclarationSyntax@L29740`, `grammar.StaticDeclarationSyntax@L30250`, `req.StaticDeclParserOwnership@L30283`, `grammar.ExternBlockShellSyntax@L31118`

#### `parser.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L30797-L30797.

- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31138`
- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31138`

#### `parser.modules`

Count: 58 total; 58 required; 0 recommended; 0 informative. Ledger line span: L31081-L32075.

- `grammar.ModulePathSyntax@L31422`, `req.ModuleToFileMappingNoSurfaceSyntax@L31441`, `req.ModulePathParserOwnership@L31455`, `Parse-ModulePath@L31471`, `Parse-ModulePathTail-End@L31489`, `Parse-ModulePathTail-Cons@L31507`, `def.PathAstAliases@L31525`, `def.ASTModule@L31546`
- `def.ASTFile@L31563`, `Module-Path-Root@L31599`, `Module-Path-Rel@L31617`, `Module-Path-Rel-Fail@L31635`, `def.ModuleDirOf@L31653`, `def.ProjectModuleView@L31668`, `def.SourceRootOfModule@L31686`, `def.WFModulePathJudgementSet@L31700`
- `WF-Module-Path-Ok@L31714`, `WF-Module-Path-Reserved@L31732`, `WF-Module-Path-Ident-Err@L31750`, `WF-Module-Path-Collision@L31768`, `def.ModuleAggregationInputsOutputs@L31786`, `def.ModuleMap@L31803`, `def.ASTModuleOfProjectPath@L31817`, `def.PathOfModule@L31831`
- `def.ParseModuleRuleReference@L31861`, `def.ParseModuleBigStepInputs@L31875`, `ReadBytes-Ok@L31892`, `ReadBytes-Err@L31910`, `def.BytesOfFile@L31928`, `ParseModule-Ok@L31942`, `ParseModule-Err-Read@L31960`, `ParseModule-Err-Load@L31978`
- `req.LoadSourceShortCircuit@L31996`, `ParseModule-Err-Unit@L32011`, `ParseModule-Err-Parse@L32029`, `def.ParseFileBestEffort@L32047`, `def.ParseFileOk@L32061`, `def.ParseFileDiag@L32075`, `def.HasErrorDiagnostics@L32089`, `def.ModState@L32103`
- `Mod-Start@L32118`, `Mod-Start-Err-Unit@L32136`, `Mod-Scan@L32154`, `Mod-Scan-Err-Read@L32172`, `Mod-Scan-Err-Load@L32190`, `Mod-Scan-Err-Parse@L32208`, `Mod-Done@L32226`, `def.ParseModulesBigStepInputs@L32243`
- `ParseModules-Ok@L32259`, `ParseModules-Err@L32277`, `def.DiscState@L32295`, `Disc-Start@L32309`, `Disc-Skip@L32326`, `Disc-Add@L32344`, `Disc-Collision@L32362`, `Disc-Invalid-Component@L32380`
- `Disc-Rel-Fail@L32398`, `Disc-Done@L32416`
- `grammar.ModulePathSyntax@L31422`, `req.ModuleToFileMappingNoSurfaceSyntax@L31441`, `req.ModulePathParserOwnership@L31455`, `Parse-ModulePath@L31471`, `Parse-ModulePathTail-End@L31489`, `Parse-ModulePathTail-Cons@L31507`, `def.PathAstAliases@L31525`, `def.ASTModule@L31546`
- `def.ASTFile@L31563`, `Module-Path-Root@L31599`, `Module-Path-Rel@L31617`, `Module-Path-Rel-Fail@L31635`, `def.ModuleDirOf@L31653`, `def.ProjectModuleView@L31668`, `def.SourceRootOfModule@L31686`, `def.WFModulePathJudgementSet@L31700`
- `WF-Module-Path-Ok@L31714`, `WF-Module-Path-Reserved@L31732`, `WF-Module-Path-Ident-Err@L31750`, `WF-Module-Path-Collision@L31768`, `def.ModuleAggregationInputsOutputs@L31786`, `def.ModuleMap@L31803`, `def.ASTModuleOfProjectPath@L31817`, `def.PathOfModule@L31831`
- `def.ParseModuleRuleReference@L31861`, `def.ParseModuleBigStepInputs@L31875`, `ReadBytes-Ok@L31892`, `ReadBytes-Err@L31910`, `def.BytesOfFile@L31928`, `ParseModule-Ok@L31942`, `ParseModule-Err-Read@L31960`, `ParseModule-Err-Load@L31978`
- `req.LoadSourceShortCircuit@L31996`, `ParseModule-Err-Unit@L32011`, `ParseModule-Err-Parse@L32029`, `def.ParseFileBestEffort@L32047`, `def.ParseFileOk@L32061`, `def.ParseFileDiag@L32075`, `def.HasErrorDiagnostics@L32089`, `def.ModState@L32103`
- `Mod-Start@L32118`, `Mod-Start-Err-Unit@L32136`, `Mod-Scan@L32154`, `Mod-Scan-Err-Read@L32172`, `Mod-Scan-Err-Load@L32190`, `Mod-Scan-Err-Parse@L32208`, `Mod-Done@L32226`, `def.ParseModulesBigStepInputs@L32243`
- `ParseModules-Ok@L32259`, `ParseModules-Err@L32277`, `def.DiscState@L32295`, `Disc-Start@L32309`, `Disc-Skip@L32326`, `Disc-Add@L32344`, `Disc-Collision@L32362`, `Disc-Invalid-Component@L32380`
- `Disc-Rel-Fail@L32398`, `Disc-Done@L32416`

#### `parser.types`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L34302-L40557.

- `grammar.PrimitiveTypeSyntax@L34643`, `def.PrimLexemeSet@L34670`, `grammar.TupleSyntax@L34984`, `req.TupleSingletonCommaIllFormed@L35008`, `def.TupleScanDepthAndStep@L35120`, `def.TupleScanPredicates@L35136`, `grammar.ArraySyntax@L35847`, `grammar.SliceSyntax@L36545`
- `grammar.RangeSyntax@L37114`, `req.RangeTypeParserOwnership@L37138`, `grammar.RecordSyntax@L38191`, `grammar.EnumSyntax@L39166`, `req.EnumVariantSeparatorSyntax@L39191`, `req.EnumTopLevelCommaSeparatorRejected@L39423`, `grammar.UnionTypeSyntax@L40276`, `grammar.TypeAliasSyntax@L40898`
- `grammar.PrimitiveTypeSyntax@L34643`, `def.PrimLexemeSet@L34670`, `grammar.TupleSyntax@L34984`, `req.TupleSingletonCommaIllFormed@L35008`, `def.TupleScanDepthAndStep@L35120`, `def.TupleScanPredicates@L35136`, `grammar.ArraySyntax@L35847`, `grammar.SliceSyntax@L36545`
- `grammar.RangeSyntax@L37114`, `req.RangeTypeParserOwnership@L37138`, `grammar.RecordSyntax@L38191`, `grammar.EnumSyntax@L39166`, `req.EnumVariantSeparatorSyntax@L39191`, `req.EnumTopLevelCommaSeparatorRejected@L39423`, `grammar.UnionTypeSyntax@L40276`, `grammar.TypeAliasSyntax@L40898`

#### `spec.grammar`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L98736-L99400.

- `grammar.B.1.LexicalGrammar@L99152`, `grammar.B.2.TypeGrammar@L99202`, `req.B.2.ClosureTypeUnionParameterParentheses@L99262`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99275`, `grammar.B.3.ExpressionGrammar@L99308`, `req.B.3.ClosureExprUnionParameterParentheses@L99386`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99399`, `grammar.B.4.PatternGrammar@L99433`
- `grammar.B.5.StatementGrammar@L99463`, `grammar.B.6.DeclarationGrammar@L99500`, `grammar.B.7.ContractGrammar@L99584`, `grammar.B.8.AttributeGrammar@L99611`, `grammar.B.9.KeySystemGrammar@L99667`, `grammar.B.10.ConcurrencyGrammar@L99697`, `grammar.B.11.AsyncGrammar@L99731`, `grammar.B.12.MetaprogrammingGrammar@L99760`
- `grammar.B.13.FFIGrammar@L99793`, `grammar.B.14.RegionGrammar@L99819`
- `grammar.B.1.LexicalGrammar@L99152`, `grammar.B.2.TypeGrammar@L99202`, `req.B.2.ClosureTypeUnionParameterParentheses@L99262`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99275`, `grammar.B.3.ExpressionGrammar@L99308`, `req.B.3.ClosureExprUnionParameterParentheses@L99386`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99399`, `grammar.B.4.PatternGrammar@L99433`
- `grammar.B.5.StatementGrammar@L99463`, `grammar.B.6.DeclarationGrammar@L99500`, `grammar.B.7.ContractGrammar@L99584`, `grammar.B.8.AttributeGrammar@L99611`, `grammar.B.9.KeySystemGrammar@L99667`, `grammar.B.10.ConcurrencyGrammar@L99697`, `grammar.B.11.AsyncGrammar@L99731`, `grammar.B.12.MetaprogrammingGrammar@L99760`
- `grammar.B.13.FFIGrammar@L99793`, `grammar.B.14.RegionGrammar@L99819`

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
- `Prim-File-Flush-Append@L14227`, `Prim-File-Close-Read@L14245`, `Prim-File-Close-Write@L14263`, `Prim-File-Close-Append@L14281`, `Prim-Dir-Next@L14299`, `Prim-Dir-Close@L14317`, `Prim-System-GetEnv@L14335`, `Prim-System-Exit@L14407`
- `Prim-System-Run@L14425`, `Prim-Network-RestrictHost@L14443`, `req.PrimSystemExitAbortOutcome@L14461`

#### `runtime.binding-store`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L16858-L17386.

- `def.ScopeEntry@L16912`, `def.DynamicScopeStack@L16931`, `def.UpdateScopeStack@L16949`, `def.RuntimeScopePushPop@L16963`, `def.AppendCleanup@L16978`, `def.CleanupList@L16992`, `def.ScopeById@L17006`, `def.ReplaceScopeById@L17023`
- `def.SetCleanupList@L17040`, `def.PoisonedModule@L17054`, `req.HostedSessionPoisonFlagLocalization@L17068`, `def.PoisonedModules@L17082`, `def.RuntimeBindingIdentityAndValue@L17096`, `def.FreshBindId@L17111`, `def.Last@L17125`, `def.NearestScope@L17140`
- `def.LookupBind@L17157`, `def.RuntimeBindingValueLookup@L17171`, `def.RuntimeBindingStateLookup@L17185`, `LookupVal-Bind-Value@L17199`, `LookupVal-Bind-Alias@L17217`, `LookupVal-Path@L17235`, `LookupValPath-Builtin@L17253`, `LookupValPath-Static@L17271`
- `LookupValPath-Proc@L17289`, `LookupValPath-RecordCtor@L17307`, `def.ScopeValueAndStateUpdate@L17325`, `def.UpdateRuntimeBindingValue@L17340`, `def.SetRuntimeBindingState@L17354`, `def.RuntimeBindingTypeAndInfo@L17368`, `def.BindRuntimeValue@L17383`, `def.BindPatternValue@L17397`
- `def.PatternBindingOrder@L17411`, `def.BindRuntimeList@L17425`, `def.BindPattern@L17440`

#### `runtime.regions`

Count: 45 total; 45 required; 0 recommended; 0 informative. Ledger line span: L17402-L18064.

- `def.RuntimeRegionEntry@L17456`, `def.RuntimeAddressTags@L17474`, `def.RuntimeRegionStack@L17489`, `def.RegionArena@L17503`, `def.UpdateRegionArena@L17518`, `def.ArenaNew@L17532`, `def.FreshRuntimeAddress@L17546`, `def.Prefix@L17560`
- `def.ArenaAppend@L17574`, `def.ArenaMark@L17588`, `def.ArenaResetTo@L17602`, `def.ArenaClear@L17616`, `def.ArenaRemove@L17630`, `def.RegionValue@L17644`, `def.ResolveRuntimeRegionEntry@L17659`, `def.ActiveRuntimeRegion@L17676`
- `def.ResolveRuntimeRegionTargetAndTag@L17691`, `def.FreshRuntimeRegionTagAndArena@L17706`, `def.UpdateRegionStack@L17721`, `def.RegionNew@L17735`, `def.RegionOpen@L17749`, `def.FrameEnter@L17763`, `def.BindRegionAlias@L17777`, `def.TagAddr@L17792`
- `def.TagAddrFrom@L17806`, `def.RegionAlloc@L17820`, `def.FreshRuntimeRegionTags@L17834`, `def.RetagRegions@L17848`, `def.RegionReset@L17865`, `def.PopRegions@L17879`, `def.RegionFree@L17896`, `def.FrameMark@L17910`
- `def.PopRegionScope@L17924`, `def.ReleaseArena@L17941`, `def.ResetArena@L17955`, `req.RegionRuntimeOwnershipBoundary@L17969`, `req.RegionReleaseCleanupBeforeArenaReclaim@L17985`, `req.ArenaReclaimNoDrop@L18000`, `def.RegionProcedureJudgements@L18014`, `Region-New-Scoped@L18028`
- `Region-Alloc-Proc@L18046`, `Region-Reset-Proc@L18064`, `Region-Freeze-Proc@L18082`, `Region-Thaw-Proc@L18100`, `Region-Free-Proc@L18118`

#### `runtime.value-model`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L18082-L18347.

- `def.BlockEnter@L18136`, `def.ScalarRuntimeValues@L18150`, `def.PointerRuntimeValues@L18172`, `def.AggregateRuntimeValues@L18186`, `def.RuntimeValueDomain@L18209`, `def.TupleValueOperations@L18223`, `def.RecordFieldValueOperations@L18238`, `def.IndexAndSliceValueOperations@L18253`
- `def.AddressPrimitiveJudgments@L18271`, `def.AddressArithmetic@L18285`, `def.ElementType@L18299`, `def.AggregateAddressCalculation@L18313`, `def.PointerStateAndAddress@L18332`, `def.BindingAddresses@L18351`, `def.RuntimeAddressTagLookup@L18369`, `def.RuntimeTagActive@L18386`
- `def.DynamicAddressState@L18401`

#### `runtime.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27083-L27292.

- `conformance.AttributeDynamicSemantics@L27139`, `conformance.VendorAttributeDynamicSemantics@L27348`
- `conformance.AttributeDynamicSemantics@L27139`, `conformance.VendorAttributeDynamicSemantics@L27348`

#### `runtime.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27579-L27579.

- `conformance.LayoutAttributeDynamicSemantics@L27635`
- `conformance.LayoutAttributeDynamicSemantics@L27635`

#### `runtime.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27724-L27724.

- `conformance.OptimizationAttributeDynamicSemantics@L27780`
- `conformance.OptimizationAttributeDynamicSemantics@L27780`

#### `runtime.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28203-L28203.

- `conformance.DiagnosticsMetadataDynamicSemantics@L28259`
- `conformance.DiagnosticsMetadataDynamicSemantics@L28259`

#### `runtime.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28451-L28670.

- `conformance.PermissionDynamicSemantics@L28792`, `conformance.AliasExclusivityDynamicSemantics@L29011`
- `conformance.PermissionDynamicSemantics@L28792`, `conformance.AliasExclusivityDynamicSemantics@L29011`

#### `runtime`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L29083-L40526.

- `conformance.PermissionAdmissibilityRuntimeIdentity@L29424`, `conformance.ImportDeclarationDynamicSemantics@L29673`, `conformance.UsingDeclarationDynamicSemantics@L30180`, `conformance.StaticDeclarationDynamicSemantics@L30473`, `req.HostedLibraryStaticAddrSessionLocal@L30767`, `conformance.ExternBlockDynamicSemantics@L31371`, `conformance.ModuleAggregationDynamicSemantics@L34568`, `def.PrimitiveValueTypes@L34894`
- `req.PrimitiveOperationEvaluationOwnership@L34917`, `def.TupleValueType@L35598`, `def.ArrayValueType@L36319`, `def.ArrayIndexRuntimeHelpers@L36335`, `def.SliceValueType@L36929`, `def.SliceRuntimeIndexHelpers@L36945`, `def.SliceIndexUpdate@L37063`, `def.RangeValueTypes@L37588`
- `def.SliceBoundsRaw@L37677`, `def.SliceBounds@L37696`, `def.RecordValueType@L38785`, `def.RecordDefaultInits@L38837`, `def.EnumValueType@L39960`, `def.UnionCase@L40518`, `def.UnionValueType@L40867`
- `conformance.PermissionAdmissibilityRuntimeIdentity@L29424`, `conformance.ImportDeclarationDynamicSemantics@L29673`, `conformance.UsingDeclarationDynamicSemantics@L30180`, `conformance.StaticDeclarationDynamicSemantics@L30473`, `req.HostedLibraryStaticAddrSessionLocal@L30767`, `conformance.ExternBlockDynamicSemantics@L31371`, `conformance.ModuleAggregationDynamicSemantics@L34568`, `def.PrimitiveValueTypes@L34894`
- `req.PrimitiveOperationEvaluationOwnership@L34917`, `def.TupleValueType@L35598`, `def.ArrayValueType@L36319`, `def.ArrayIndexRuntimeHelpers@L36335`, `def.SliceValueType@L36929`, `def.SliceRuntimeIndexHelpers@L36945`, `def.SliceIndexUpdate@L37063`, `def.RangeValueTypes@L37588`
- `def.SliceBoundsRaw@L37677`, `def.SliceBounds@L37696`, `def.RecordValueType@L38785`, `def.RecordDefaultInits@L38837`, `def.EnumValueType@L39960`, `def.UnionCase@L40518`, `def.UnionValueType@L40867`

### Modules, Name Resolution, And Visibility

#### `checker.name-resolution`

Count: 307 total; 307 required; 0 recommended; 0 informative. Ledger line span: L18399-L23682.

- `def.ScopeKeyConstraint@L18453`, `def.GlobalResolutionTables@L18468`, `def.ResolutionContext@L18485`, `def.ResolutionEntity@L18503`, `def.ResolutionScope@L18521`, `def.ResolutionScopeStack@L18535`, `def.UniverseBindings@L18553`, `def.BytePrefix@L18568`
- `def.ReservedIdentifiers@L18583`, `def.ReservedModulePath@L18599`, `def.BuiltinTypeNameSets@L18615`, `req.PredicateNameUniverseReservation@L18631`, `def.NameResolutionKeywordKeys@L18645`, `def.ScopeDomain@L18665`, `def.NameIntroductionScopeSequence@L18679`, `def.InScope@L18693`
- `def.InOuter@L18707`, `Intro-Ok@L18721`, `Intro-Dup@L18739`, `Intro-Outer-Err@L18757`, `Intro-Reserved-Gen-Err@L18775`, `Intro-Reserved-Ultraviolet-Err@L18793`, `req.IntroRulePriority@L18811`, `def.UsingAlias@L18827`
- `Using-Alias-Ok@L18843`, `Using-Alias-Unresolved@L18861`, `Using-Alias-Dup@L18879`, `Using-Alias-Reserved@L18897`, `req.UsingAliasRulePriority@L18915`, `def.ModuleNameValidationHelpers@L18929`, `Validate-Module-Ok@L18943`, `Validate-Module-Keyword-Err@L18961`
- `req.UniverseScopeReuseHandledByIntro@L18979`, `def.LookupScopeSequence@L18995`, `def.LookupNearestScopeIndex@L19009`, `Lookup-Unqualified@L19023`, `Lookup-Unqualified-None@L19041`, `def.EntityKindPredicates@L19059`, `def.RegionAliasName@L19077`, `Resolve-Value-Name@L19091`
- `Resolve-Type-Name@L19109`, `Resolve-Class-Name@L19127`, `Resolve-Module-Name@L19145`, `def.QualifiedResolutionProjectInput@L19163`, `def.QualifiedResolutionCurrentModule@L19177`, `def.QualifiedResolutionVisibleModules@L19191`, `def.QualifiedResolutionAliasMap@L19206`, `def.QualifiedResolutionKindDomain@L19220`
- `req.ModuleVisibilityJudgementOwnership@L19234`, `req.ResolveModulePathCanonicalOwner@L19248`, `Resolve-Qualified@L19262`, `prop.CollectNamesOrderIndependence@L19518`, `def.BindingKindDomain@L19532`, `def.BindingSourceDomain@L19546`, `def.NameInfoShape@L19560`, `def.ModuleNameMap@L19574`
- `def.AliasMap@L19588`, `def.UsingMap@L19602`, `def.UsingValueMap@L19616`, `def.UsingTypeMap@L19630`, `def.TypeMap@L19644`, `def.ClassMap@L19658`, `PatNames-IdentifierPattern@L19672`, `PatNames-WildcardPattern@L19687`
- `PatNames-LiteralPattern@L19702`, `PatNames-TuplePattern@L19717`, `PatNames-RecordFieldPattern@L19734`, `PatNames-RecordFieldShorthand@L19751`, `PatNames-RecordPattern@L19766`, `PatNames-EnumNoPayload@L19783`, `PatNames-EnumTuplePayload@L19798`, `PatNames-EnumRecordPayload@L19815`
- `PatNames-RangePattern@L19832`, `def.AllModuleNames@L19849`, `def.VisibleModuleNames@L19863`, `def.LastPathComponent@L19877`, `def.IsModulePath@L19891`, `def.SplitLastPathComponent@L19905`, `def.ModuleByPath@L32678`, `def.ItemNames@L32706`
- `def.UsingSpecName@L29971`, `def.UsingSpecNames@L29987`, `DeclNames-Empty@L19977`, `DeclNames-Using@L19992`, `DeclNames-Item@L20010`, `def.ModuleDeclNames@L20028`, `req.ImportUsingJudgementCanonicalOwner@L20042`, `Bind-Procedure@L20056`
- `Bind-ExternBlock@L31299`, `Bind-Record@L38497`, `Bind-Enum@L39506`, `Bind-Class@L20125`, `Bind-TypeAlias@L40972`, `Bind-Static@L30365`, `Bind-Import@L29620`, `Bind-Import-Err@L29638`
- `Bind-Using@L30127`, `Bind-Using-Err@L30145`, `Bind-ErrorItem@L20249`, `Collect-Ok@L20266`, `Collect-Scan@L20284`, `Collect-Using-Import-Dup@L20302`, `Collect-Dup@L20320`, `Collect-Err@L20338`
- `PatNames-RangePattern@L19832`, `def.AllModuleNames@L19849`, `def.VisibleModuleNames@L19863`, `def.LastPathComponent@L19877`, `def.IsModulePath@L19891`, `def.SplitLastPathComponent@L19905`, `def.ModuleByPath@L32678`, `def.ItemNames@L32706`
- `def.UsingSpecName@L29971`, `def.UsingSpecNames@L29987`, `DeclNames-Empty@L19977`, `DeclNames-Using@L19992`, `DeclNames-Item@L20010`, `def.ModuleDeclNames@L20028`, `req.ImportUsingJudgementCanonicalOwner@L20042`, `Bind-Procedure@L20056`
- `Bind-ExternBlock@L31299`, `Bind-Record@L38497`, `Bind-Enum@L39506`, `Bind-Class@L20125`, `Bind-TypeAlias@L40972`, `Bind-Static@L30365`, `Bind-Import@L29620`, `Bind-Import-Err@L29638`
- `Bind-Using@L30127`, `Bind-Using-Err@L30145`, `Bind-ErrorItem@L20249`, `Collect-Ok@L20266`, `Collect-Scan@L20284`, `Collect-Using-Import-Dup@L20302`, `Collect-Dup@L20320`, `Collect-Err@L20338`
- `def.BindingNameSet@L20356`, `def.NoDuplicateBindingNames@L20370`, `def.DisjointBindingNames@L20384`, `def.NameMapUnion@L20398`, `def.NameInfoOfBinding@L20412`, `def.BindingNameSource@L20426`, `def.NameMapSource@L20440`, `def.UsingImportConflict@L20454`
- `def.NameCollectionStateDomain@L20468`, `Names-Start@L20482`, `Names-Step@L20499`, `Names-Step-Using-Import-Dup@L20517`, `Names-Step-Dup@L20535`, `Names-Step-Err@L20553`, `Names-Done@L20571`, `def.ResolveQualifiedFormSignature@L20590`
- `def.ResolveArgsSignature@L20604`, `def.ResolveFieldInitsSignature@L20618`, `def.ResolveRecordPathSignature@L20632`, `def.ResolveEnumUnitSignature@L20646`, `def.ResolveEnumTupleSignature@L20660`, `def.ResolveEnumRecordSignature@L20674`, `def.ResolvePathJudgementSet@L20688`, `ResolveArgs-Empty@L20702`
- `ResolveArgs-Cons@L20719`, `ResolveFieldInits-Empty@L20737`, `ResolveFieldInits-Cons@L20754`, `Resolve-RecordPath@L38514`, `Resolve-EnumUnit@L39541`, `Resolve-EnumTuple@L39559`, `Resolve-EnumRecord@L39577`, `def.BuiltinValuePath@L20844`
- `ResolveQual-Name-Builtin@L20858`, `ResolveQual-Name-Value@L20876`, `ResolveQual-Name-Record@L38532`, `ResolveQual-Name-Enum@L39595`, `ResolveQual-Name-Err@L20930`, `def.SharedResolutionProjectInput@L20951`, `def.SharedResolutionCurrentModule@L20965`, `def.SharedResolutionAstModule@L20979`
- `ResolveArgs-Cons@L20719`, `ResolveFieldInits-Empty@L20737`, `ResolveFieldInits-Cons@L20754`, `Resolve-RecordPath@L38514`, `Resolve-EnumUnit@L39541`, `Resolve-EnumTuple@L39559`, `Resolve-EnumRecord@L39577`, `def.BuiltinValuePath@L20844`
- `ResolveQual-Name-Builtin@L20858`, `ResolveQual-Name-Value@L20876`, `ResolveQual-Name-Record@L38532`, `ResolveQual-Name-Enum@L39595`, `ResolveQual-Name-Err@L20930`, `def.SharedResolutionProjectInput@L20951`, `def.SharedResolutionCurrentModule@L20965`, `def.SharedResolutionAstModule@L20979`
- `def.SharedResolutionInputs@L20993`, `def.SharedResolutionOutputs@L21007`, `def.PathOfModuleReference@L21021`, `def.TypeParamBindings@L21035`, `ResolveGenericParamsOpt-None@L21050`, `ResolvePredicateClauseOpt-None@L21065`, `ResolveContractClauseOpt-None@L21080`, `ResolveInvariantOpt-None@L21095`
- `ResolveTypeOpt-None@L21110`, `ResolveExprOpt-None-Judgement@L21125`, `def.ResolveExprOptNone@L21140`, `def.ResolveExprOptSome@L21154`, `ResolveGenericParamsOpt-Yes@L21168`, `ResolveTypeParam@L21186`, `ResolveTypeParamList-Empty@L21204`, `ResolveTypeParamList-Cons@L21219`
- `ResolvePredicateClauseOpt-Yes@L21237`, `ResolvePredicateReqList-Empty@L21255`, `ResolvePredicateReq-Predicate@L21270`, `ResolvePredicateReqList-Cons@L21288`, `ResolveContractClauseOpt-Yes@L21306`, `ResolveInvariantOpt-Yes@L21324`, `ResolveTypePath-Ident@L21342`, `ResolveTypePath-Ident-Local@L21360`
- `ResolveTypePath-Qual@L21378`, `def.LocalTypePath@L21396`, `ResolveClassPath-Ident@L21410`, `ResolveClassPath-Qual@L21428`, `ResolveType-Path@L21446`, `ResolveType-Dynamic@L21464`, `ResolveType-Apply@L21482`, `ResolveType-ModalState@L21500`
- `def.ResolveModalRef@L21518`, `ResolveType-Hom@L21534`, `ResolveTypeList-Empty@L21552`, `ResolveTypeList-Cons@L21567`, `ResolveParam@L21585`, `ResolveParams-Empty@L21603`, `ResolveParams-Cons@L21618`, `def.ResolvePatternSignature@L21637`
- `ResolvePat-Wildcard@L21651`, `ResolvePat-Identifier@L21666`, `ResolvePat-Literal@L21681`, `ResolvePat-Tuple@L21696`, `ResolvePat-Record@L21714`, `ResolvePat-Enum@L21732`, `ResolvePat-Modal@L21750`, `ResolvePat-Range@L21768`
- `ResolvePatternList-Empty@L21786`, `ResolveFieldPatternList-Empty@L21801`, `ResolvePatternList-Cons@L21816`, `ResolveFieldPattern-Implicit@L21834`, `ResolveFieldPattern-Explicit@L21851`, `ResolveFieldPatternList-Cons@L21869`, `ResolveEnumPayloadPattern-None@L21887`, `ResolveEnumPayloadPattern-Tuple@L21902`
- `ResolveEnumPayloadPattern-Record@L21920`, `ResolveFieldPatternListOpt-None@L21938`, `ResolveFieldPatternListOpt-Some@L21953`, `ResolveExpr-Ident@L21972`, `ResolveExpr-Ident-Err@L21990`, `ResolveExpr-Qualified@L22008`, `def.ResolveArgsReference@L22026`, `def.ResolveFieldInitsReference@L22040`
- `ResolveExprList-Empty@L22054`, `ResolveExprList-Cons@L22069`, `def.ResolveExprListJudgementSet@L22087`, `def.ResolveEnumPayloadJudgementSet@L22101`, `ResolveEnumPayload-None@L22115`, `ResolveEnumPayload-Tuple@L22132`, `ResolveEnumPayload-Record@L22150`, `def.ResolveKeyPathJudgementSet@L22168`
- `ResolveKeySeg-Field@L22182`, `ResolveKeySeg-Index@L22200`, `ResolveKeySegs-Empty@L22218`, `ResolveKeySegs-Cons@L22235`, `ResolveKeyPathExpr@L22253`, `ResolveKeyPathExpr-Err@L22271`, `ResolveKeyPathList-Empty@L22289`, `ResolveKeyPathList-Cons@L22306`
- `def.ResolveParallelOptJudgementSet@L22324`, `ResolveParallelOpt-Cancel@L22338`, `ResolveParallelOpt-Name@L22356`, `ResolveParallelOpts-Empty@L22373`, `ResolveParallelOpts-Cons@L22390`, `def.ResolveSpawnOptJudgementSet@L22408`, `ResolveSpawnOpt-Name@L22422`, `ResolveSpawnOpt-Affinity@L22439`
- `ResolveSpawnOpt-Priority@L22457`, `ResolveSpawnOpts-Empty@L22475`, `ResolveSpawnOpts-Cons@L22492`, `def.ResolveDispatchOptJudgementSet@L22510`, `ResolveDispatchOpt-Reduce@L22524`, `ResolveDispatchOpt-Ordered@L22541`, `ResolveDispatchOpt-Chunk@L22558`, `ResolveDispatchOpts-Empty@L22576`
- `ResolveDispatchOpts-Cons@L22593`, `def.ResolveRaceJudgementSet@L22611`, `ResolveRaceHandler-Return@L22625`, `ResolveRaceHandler-Yield@L22643`, `ResolveRaceArm@L22661`, `ResolveRaceArms-Empty@L22679`, `ResolveRaceArms-Cons@L22696`, `def.ResolveAllExprListJudgementSet@L22714`
- `ResolveAllExprList-Empty@L22728`, `ResolveAllExprList-Cons@L22745`, `def.ResolveCalleeJudgementSet@L22763`, `ResolveCallee-Ident-Value@L22777`, `ResolveCallee-Ident-Record@L22795`, `ResolveCallee-Path-Value@L22813`, `ResolveCallee-Path-Builtin@L22831`, `ResolveCallee-Path-Record@L22849`
- `ResolveCallee-Other@L22867`, `ResolveExpr-Call@L22885`, `ResolveExpr-Call-TypeArgs@L22903`, `ResolveExpr-RecordExpr@L22922`, `ResolveExpr-EnumLiteral@L22940`, `def.ResolveIfCaseJudgementSet@L22958`, `ResolveIfCase@L22972`, `ResolveIfCases-Empty@L22990`
- `ResolveIfCases-Cons@L23007`, `ResolveElseBlockOpt-None@L23025`, `ResolveElseBlockOpt-Some@L23042`, `ResolveExpr-IfIs@L23060`, `ResolveExpr-IfCase@L23078`, `ResolveExpr-LoopInfinite@L23096`, `ResolveExpr-LoopConditional@L23114`, `ResolveExpr-LoopIter@L23132`
- `ResolveExpr-Parallel@L23150`, `ResolveExpr-Spawn@L23168`, `ResolveExpr-Wait@L23186`, `def.ResolveKeyClauseJudgementSet@L23204`, `ResolveKeyClauseOpt-None@L23218`, `ResolveKeyClauseOpt-Yes@L23236`, `ResolveExpr-Dispatch@L23254`, `ResolveExpr-Yield@L23272`
- `ResolveExpr-YieldFrom@L23290`, `ResolveExpr-Sync@L23308`, `ResolveExpr-Race@L23326`, `ResolveExpr-All@L23344`, `ResolveExpr-Alloc-Explicit-ByAlias@L23362`, `def.ResolveExprRuleSet@L23380`, `def.NoSpecificResolveExpr@L23394`, `ResolveExpr-Hom@L23408`
- `ResolveExpr-Alloc-Implicit@L23426`, `ResolveExpr-Alloc-Explicit@L23444`, `def.ResolveStmtSeqJudgementSet@L23462`, `ResolveStmtSeq-Empty@L23476`, `ResolveStmtSeq-Cons@L23493`, `ResolveExpr-Block@L23511`, `Validate-ModulePath-Ok@L23530`, `Validate-ModulePath-Reserved-Err@L23548`
- `req.ResolveItemFeatureOwnership@L23566`, `ResolveModule-Ok@L23580`, `ResolveItems-Empty@L23598`, `ResolveItems-Cons@L23613`, `def.ResolutionStateDomain@L23631`, `Res-Start@L23645`, `Res-Names@L23662`, `Res-Items@L23680`
- `ResolveModules-Ok@L23700`, `ResolveModules-Err-Parse@L23718`, `ResolveModules-Err-Resolve@L23736`

#### `checker.visibility`

Count: 15 total; 15 required; 0 recommended; 0 informative. Ledger line span: L19228-L19444.

- `def.DeclOfModuleItem@L19282`, `def.DeclOfExternProc@L19296`, `def.ModuleOfItem@L19310`, `def.ModuleOfExternProc@L19324`, `def.ExternBlockOfProc@L19338`, `def.ExternProcName@L19352`, `def.VisibilityOfDeclaration@L19366`, `def.SameAssembly@L19380`
- `Access-Public@L19394`, `Access-Internal@L19412`, `Access-Private@L19430`, `Access-Internal-Err@L19448`, `Access-Err@L19466`, `def.TopLevelDeclarationPredicate@L19484`, `TopLevelVis-Ok@L19498`

#### `checker.modules`

Count: 209 total; 209 required; 0 recommended; 0 informative. Ledger line span: L29191-L34273.

- `Parse-Import@L29532`, `def.ImportDeclAst@L29550`, `req.ImportDeclarationBindingSemanticsScope@L29568`, `Import-Path@L29584`, `Import-Path-Err@L29602`, `Bind-Import@L29620`, `Bind-Import-Err@L29638`, `ResolveItem-Import@L29656`
- `diagnostics.ImportDeclarations@L29705`, `req.ImportDeclarationDiagnosticOwnership@L29723`, `Parse-Using-Wildcard@L29767`, `Parse-Using-List@L29785`, `Parse-Using-Item@L29803`, `Parse-UsingSpec@L29821`, `Parse-UsingList-Empty@L29839`, `Parse-UsingList-Cons@L29857`
- `Parse-UsingListTail-End@L29875`, `Parse-UsingListTail-TrailingComma@L29893`, `Parse-UsingListTail-Comma@L29911`, `def.UsingDeclAst@L29929`, `req.UsingDeclarationBindingSemanticsScope@L29955`, `def.UsingSpecName@L29971`, `def.UsingSpecNames@L29987`, `Using-Item@L30001`
- `Using-Item-Public-Err@L30019`, `Using-List@L30037`, `Using-Wildcard-Warn@L30055`, `Using-Wildcard@L30073`, `Using-List-Dup@L30091`, `Using-List-Public-Err@L30109`, `Bind-Using@L30127`, `Bind-Using-Err@L30145`
- `ResolveItem-Using@L30163`, `diagnostics.UsingDeclarations@L30212`, `req.UsingDeclarationDiagnosticOwnership@L30233`, `def.StaticDeclTopLevelItems@L30269`, `Parse-Static-Decl@L30299`, `def.StaticDeclAst@L30317`, `req.StaticDeclModuleScopeBindingSemantics@L30335`, `def.StaticVisOk@L30351`
- `Bind-Static@L30365`, `WF-StaticDecl@L30383`, `WF-StaticDecl-Ann-Mismatch@L30401`, `WF-StaticDecl-MissingType@L30419`, `StaticVisOk-Err@L30437`, `ResolveItem-Static@L30455`, `def.StaticBindTypes@L30535`, `def.StaticBindList@L30549`
- `Emit-Static-Const@L30593`, `Emit-Static-Init@L30611`, `Emit-Static-Multi@L30629`, `InitFn@L30661`, `DeinitFn@L30693`, `def.StaticItems@L30711`, `def.StaticItemOf@L30725`, `def.StaticType@L30795`
- `def.StaticBindInfo@L30809`, `Lower-StaticInit-Item@L30853`, `Lower-StaticInitItems-Empty@L30871`, `Lower-StaticInitItems-Cons@L30888`, `Lower-StaticInit@L30906`, `InitCallIR@L30924`, `Lower-StaticDeinitNames-Empty@L30957`, `Lower-StaticDeinitNames-Cons-Resp@L30974`
- `Lower-StaticDeinitNames-Cons-NoResp@L30992`, `Lower-StaticDeinit-Item@L31010`, `Lower-StaticDeinitItems-Empty@L31028`, `Lower-StaticDeinitItems-Cons@L31045`, `Lower-StaticDeinit@L31063`, `diagnostics.StaticDeclarations@L31081`, `req.StaticDeclarationDiagnosticOwnership@L31101`, `Parse-ExternBlock@L31154`
- `Parse-ExternAbiOpt-None@L31172`, `Parse-ExternAbiOpt-String@L31190`, `Parse-ExternAbiOpt-Ident@L31208`, `Parse-ExternItemList-End@L31226`, `Parse-ExternItemList-Cons@L31244`, `def.ExternBlockAst@L31262`, `req.ExternBlockStaticSemanticsScope@L31283`, `Bind-ExternBlock@L31299`
- `WF-ExternBlock@L31317`, `ExternAbi-Unknown-Err@L31335`, `ResolveItem-ExternBlock@L31353`, `req.ModuleAggregationStaticSemanticsScope@L31583`, `def.NameCollectAfterParse@L31845`, `def.QualifiedLookupContext@L32434`, `def.ModuleAssemblyPathHelpers@L32451`, `def.ImportDeclarationsOfModule@L32467`
- `def.VisibleModulesAndNames@L32482`, `def.ModulePathPrefix@L32504`, `AliasExpand-None@L32520`, `AliasExpand-Yes@L32538`, `def.CurrentAsmPath@L32556`, `ModulePrefix-Direct@L32570`, `ModulePrefix-Current@L32588`, `ModulePrefix-None@L32606`
- `Resolve-ModulePath-Direct@L32624`, `Resolve-ModulePath-Current@L32642`, `ResolveModulePath-Err@L32660`, `def.ModuleByPath@L32678`, `def.ModuleOfPath@L32692`, `def.ItemNames@L32706`, `ItemOfPath@L32720`, `ItemOfPath-None@L32738`
- `def.ImportCoveragePredicates@L32756`, `def.ImportOkJudgementSet@L32771`, `Import-Ok-Local@L32785`, `Import-Ok-Covered@L32803`, `Import-Ok-Err@L32821`, `Resolve-Import-Direct@L32839`, `Resolve-Import-Current@L32857`, `Resolve-Import-Err@L32875`
- `Resolve-Using-Ok@L32893`, `Resolve-Using-Err@L32911`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32929`, `def.ModuleInitializationDependencyEnvironment@L32944`, `Reachable-Edge@L32960`, `Reachable-Step@L32978`, `def.ModuleInitializationPathHelpers@L32996`, `def.TypeRefsJudgementSet@L33012`
- `def.TypeReferenceEnvironmentAliases@L33026`, `TypeRef-Path@L33042`, `TypeRef-Using@L33060`, `TypeRef-Path-Local@L33078`, `TypeRef-Dynamic@L33096`, `TypeRef-ModalState@L33114`, `TypeRef-Apply@L33132`, `TypeRef-Perm@L33150`
- `TypeRef-Prim@L33168`, `TypeRef-Tuple@L33185`, `TypeRef-Array@L33203`, `TypeRef-Slice@L33221`, `TypeRef-Union@L33239`, `TypeRef-Func@L33257`, `TypeRef-String@L33275`, `TypeRef-Bytes@L33292`
- `TypeRef-Ptr@L33309`, `TypeRef-RawPtr@L33327`, `TypeRef-Range@L33345`, `TypeRef-RangeInclusive@L33363`, `TypeRef-RangeFrom@L33381`, `TypeRef-RangeTo@L33399`, `TypeRef-RangeToInclusive@L33417`, `TypeRef-RangeFull@L33435`
- `TypeRef-Ref-Path@L33452`, `TypeRef-Ref-Apply@L33470`, `TypeRef-Ref-ModalState@L33488`, `TypeRef-RecordExpr@L33506`, `TypeRef-EnumLiteral@L33524`, `TypeRef-QualBrace@L33542`, `TypeRef-Cast@L33560`, `TypeRef-Transmute@L33578`
- `TypeRef-CallTypeArgs@L33596`, `def.TypeRefsExprRules@L33614`, `def.NoSpecificTypeRefsExpr@L33628`, `TypeRef-Expr-Sub@L33642`, `TypeRef-RecordPattern@L33660`, `TypeRef-EnumPattern@L33678`, `TypeRef-LiteralPattern@L33696`, `TypeRef-WildcardPattern@L33713`
- `TypeRef-IdentifierPattern@L33730`, `TypeRef-TuplePattern@L33747`, `TypeRef-ModalPattern-None@L33765`, `TypeRef-ModalPattern-Record@L33782`, `TypeRef-RangePattern@L33800`, `TypeRef-Field-Explicit@L33818`, `TypeRef-Field-Implicit@L33836`, `TypeRefsExprs-Empty@L33853`
- `TypeRefsExprs-Cons@L33870`, `def.TypeRefsArgsJudgementSet@L33888`, `TypeRefsArgs-Empty@L33902`, `TypeRefsArgs-Cons@L33919`, `TypeRefsEnumPayload-None@L33937`, `TypeRefsEnumPayload-Tuple@L33954`, `TypeRefsEnumPayload-Record@L33972`, `TypeRefsFields-Empty@L33990`
- `TypeRefsFields-Cons@L34007`, `TypeRefsPayload-None@L34025`, `TypeRefsPayload-Tuple@L34042`, `TypeRefsPayload-Record@L34060`, `def.ValueReferenceEnvironmentAliases@L34078`, `def.ValueRefsJudgementSet@L34092`, `ValueRef-Ident@L34106`, `ValueRef-Ident-Local@L34124`
- `ValueRef-Qual@L34142`, `ValueRef-Qual-Local@L34160`, `ValueRef-QualApply@L34178`, `ValueRef-QualApply-Local@L34196`, `ValueRef-QualApply-Brace@L34214`, `def.ValueRefsRules@L34232`, `def.NoSpecificValueRefsExpr@L34246`, `ValueRef-Expr-Sub@L34260`
- `ValueRefsArgs-Empty@L34278`, `ValueRefsArgs-Cons@L34295`, `ValueRefsFields-Empty@L34313`, `ValueRefsFields-Cons@L34330`, `def.AstTraversalNodeHelpers@L34348`, `def.EnumVariantTypeSets@L34374`, `def.GeneralTypePositionSetHelpers@L34391`, `def.RecordMemberTypeSets@L34410`
- `def.ClassItemTypeSets@L34428`, `def.DeclarationTypePositions@L34446`, `def.TypePositionExpressions@L34467`, `def.TypeDeps@L34483`, `def.ValueDependencyExpressionSets@L34497`, `def.ValueDepsEagerLazy@L34515`, `def.ModuleDependencyGraphs@L34530`, `WF-Acyclic-Eager@L34550`
- `diagnostics.ModuleAggregation@L34614`
- `Parse-Import@L29532`, `def.ImportDeclAst@L29550`, `req.ImportDeclarationBindingSemanticsScope@L29568`, `Import-Path@L29584`, `Import-Path-Err@L29602`, `Bind-Import@L29620`, `Bind-Import-Err@L29638`, `ResolveItem-Import@L29656`
- `diagnostics.ImportDeclarations@L29705`, `req.ImportDeclarationDiagnosticOwnership@L29723`, `Parse-Using-Wildcard@L29767`, `Parse-Using-List@L29785`, `Parse-Using-Item@L29803`, `Parse-UsingSpec@L29821`, `Parse-UsingList-Empty@L29839`, `Parse-UsingList-Cons@L29857`
- `Parse-UsingListTail-End@L29875`, `Parse-UsingListTail-TrailingComma@L29893`, `Parse-UsingListTail-Comma@L29911`, `def.UsingDeclAst@L29929`, `req.UsingDeclarationBindingSemanticsScope@L29955`, `def.UsingSpecName@L29971`, `def.UsingSpecNames@L29987`, `Using-Item@L30001`
- `Using-Item-Public-Err@L30019`, `Using-List@L30037`, `Using-Wildcard-Warn@L30055`, `Using-Wildcard@L30073`, `Using-List-Dup@L30091`, `Using-List-Public-Err@L30109`, `Bind-Using@L30127`, `Bind-Using-Err@L30145`
- `ResolveItem-Using@L30163`, `diagnostics.UsingDeclarations@L30212`, `req.UsingDeclarationDiagnosticOwnership@L30233`, `def.StaticDeclTopLevelItems@L30269`, `Parse-Static-Decl@L30299`, `def.StaticDeclAst@L30317`, `req.StaticDeclModuleScopeBindingSemantics@L30335`, `def.StaticVisOk@L30351`
- `Bind-Static@L30365`, `WF-StaticDecl@L30383`, `WF-StaticDecl-Ann-Mismatch@L30401`, `WF-StaticDecl-MissingType@L30419`, `StaticVisOk-Err@L30437`, `ResolveItem-Static@L30455`, `def.StaticBindTypes@L30535`, `def.StaticBindList@L30549`
- `Emit-Static-Const@L30593`, `Emit-Static-Init@L30611`, `Emit-Static-Multi@L30629`, `InitFn@L30661`, `DeinitFn@L30693`, `def.StaticItems@L30711`, `def.StaticItemOf@L30725`, `def.StaticType@L30795`
- `def.StaticBindInfo@L30809`, `Lower-StaticInit-Item@L30853`, `Lower-StaticInitItems-Empty@L30871`, `Lower-StaticInitItems-Cons@L30888`, `Lower-StaticInit@L30906`, `InitCallIR@L30924`, `Lower-StaticDeinitNames-Empty@L30957`, `Lower-StaticDeinitNames-Cons-Resp@L30974`
- `Lower-StaticDeinitNames-Cons-NoResp@L30992`, `Lower-StaticDeinit-Item@L31010`, `Lower-StaticDeinitItems-Empty@L31028`, `Lower-StaticDeinitItems-Cons@L31045`, `Lower-StaticDeinit@L31063`, `diagnostics.StaticDeclarations@L31081`, `req.StaticDeclarationDiagnosticOwnership@L31101`, `Parse-ExternBlock@L31154`
- `Parse-ExternAbiOpt-None@L31172`, `Parse-ExternAbiOpt-String@L31190`, `Parse-ExternAbiOpt-Ident@L31208`, `Parse-ExternItemList-End@L31226`, `Parse-ExternItemList-Cons@L31244`, `def.ExternBlockAst@L31262`, `req.ExternBlockStaticSemanticsScope@L31283`, `Bind-ExternBlock@L31299`
- `WF-ExternBlock@L31317`, `ExternAbi-Unknown-Err@L31335`, `ResolveItem-ExternBlock@L31353`, `req.ModuleAggregationStaticSemanticsScope@L31583`, `def.NameCollectAfterParse@L31845`, `def.QualifiedLookupContext@L32434`, `def.ModuleAssemblyPathHelpers@L32451`, `def.ImportDeclarationsOfModule@L32467`
- `def.VisibleModulesAndNames@L32482`, `def.ModulePathPrefix@L32504`, `AliasExpand-None@L32520`, `AliasExpand-Yes@L32538`, `def.CurrentAsmPath@L32556`, `ModulePrefix-Direct@L32570`, `ModulePrefix-Current@L32588`, `ModulePrefix-None@L32606`
- `Resolve-ModulePath-Direct@L32624`, `Resolve-ModulePath-Current@L32642`, `ResolveModulePath-Err@L32660`, `def.ModuleByPath@L32678`, `def.ModuleOfPath@L32692`, `def.ItemNames@L32706`, `ItemOfPath@L32720`, `ItemOfPath-None@L32738`
- `def.ImportCoveragePredicates@L32756`, `def.ImportOkJudgementSet@L32771`, `Import-Ok-Local@L32785`, `Import-Ok-Covered@L32803`, `Import-Ok-Err@L32821`, `Resolve-Import-Direct@L32839`, `Resolve-Import-Current@L32857`, `Resolve-Import-Err@L32875`
- `Resolve-Using-Ok@L32893`, `Resolve-Using-Err@L32911`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32929`, `def.ModuleInitializationDependencyEnvironment@L32944`, `Reachable-Edge@L32960`, `Reachable-Step@L32978`, `def.ModuleInitializationPathHelpers@L32996`, `def.TypeRefsJudgementSet@L33012`
- `def.TypeReferenceEnvironmentAliases@L33026`, `TypeRef-Path@L33042`, `TypeRef-Using@L33060`, `TypeRef-Path-Local@L33078`, `TypeRef-Dynamic@L33096`, `TypeRef-ModalState@L33114`, `TypeRef-Apply@L33132`, `TypeRef-Perm@L33150`
- `TypeRef-Prim@L33168`, `TypeRef-Tuple@L33185`, `TypeRef-Array@L33203`, `TypeRef-Slice@L33221`, `TypeRef-Union@L33239`, `TypeRef-Func@L33257`, `TypeRef-String@L33275`, `TypeRef-Bytes@L33292`
- `TypeRef-Ptr@L33309`, `TypeRef-RawPtr@L33327`, `TypeRef-Range@L33345`, `TypeRef-RangeInclusive@L33363`, `TypeRef-RangeFrom@L33381`, `TypeRef-RangeTo@L33399`, `TypeRef-RangeToInclusive@L33417`, `TypeRef-RangeFull@L33435`
- `TypeRef-Ref-Path@L33452`, `TypeRef-Ref-Apply@L33470`, `TypeRef-Ref-ModalState@L33488`, `TypeRef-RecordExpr@L33506`, `TypeRef-EnumLiteral@L33524`, `TypeRef-QualBrace@L33542`, `TypeRef-Cast@L33560`, `TypeRef-Transmute@L33578`
- `TypeRef-CallTypeArgs@L33596`, `def.TypeRefsExprRules@L33614`, `def.NoSpecificTypeRefsExpr@L33628`, `TypeRef-Expr-Sub@L33642`, `TypeRef-RecordPattern@L33660`, `TypeRef-EnumPattern@L33678`, `TypeRef-LiteralPattern@L33696`, `TypeRef-WildcardPattern@L33713`
- `TypeRef-IdentifierPattern@L33730`, `TypeRef-TuplePattern@L33747`, `TypeRef-ModalPattern-None@L33765`, `TypeRef-ModalPattern-Record@L33782`, `TypeRef-RangePattern@L33800`, `TypeRef-Field-Explicit@L33818`, `TypeRef-Field-Implicit@L33836`, `TypeRefsExprs-Empty@L33853`
- `TypeRefsExprs-Cons@L33870`, `def.TypeRefsArgsJudgementSet@L33888`, `TypeRefsArgs-Empty@L33902`, `TypeRefsArgs-Cons@L33919`, `TypeRefsEnumPayload-None@L33937`, `TypeRefsEnumPayload-Tuple@L33954`, `TypeRefsEnumPayload-Record@L33972`, `TypeRefsFields-Empty@L33990`
- `TypeRefsFields-Cons@L34007`, `TypeRefsPayload-None@L34025`, `TypeRefsPayload-Tuple@L34042`, `TypeRefsPayload-Record@L34060`, `def.ValueReferenceEnvironmentAliases@L34078`, `def.ValueRefsJudgementSet@L34092`, `ValueRef-Ident@L34106`, `ValueRef-Ident-Local@L34124`
- `ValueRef-Qual@L34142`, `ValueRef-Qual-Local@L34160`, `ValueRef-QualApply@L34178`, `ValueRef-QualApply-Local@L34196`, `ValueRef-QualApply-Brace@L34214`, `def.ValueRefsRules@L34232`, `def.NoSpecificValueRefsExpr@L34246`, `ValueRef-Expr-Sub@L34260`
- `ValueRefsArgs-Empty@L34278`, `ValueRefsArgs-Cons@L34295`, `ValueRefsFields-Empty@L34313`, `ValueRefsFields-Cons@L34330`, `def.AstTraversalNodeHelpers@L34348`, `def.EnumVariantTypeSets@L34374`, `def.GeneralTypePositionSetHelpers@L34391`, `def.RecordMemberTypeSets@L34410`
- `def.ClassItemTypeSets@L34428`, `def.DeclarationTypePositions@L34446`, `def.TypePositionExpressions@L34467`, `def.TypeDeps@L34483`, `def.ValueDependencyExpressionSets@L34497`, `def.ValueDepsEagerLazy@L34515`, `def.ModuleDependencyGraphs@L34530`, `WF-Acyclic-Eager@L34550`
- `diagnostics.ModuleAggregation@L34614`

### Types, Permissions, Declarations, And Static Semantics

#### `checker.binding-state`

Count: 72 total; 72 required; 0 recommended; 0 informative. Ledger line span: L14425-L15866.

- `def.BindingStateDomain@L14479`, `def.BindInfo@L14493`, `def.BindingEnvironment@L14510`, `def.BindingScopeStackOps@L14525`, `def.BindingLookup@L14540`, `def.BindingUpdate@L14557`, `def.BindingIntro@L14574`, `def.BindingStateJoin@L14683`
- `def.BindingStateTransitionSet@L14702`, `Trans-Move-Whole@L14716`, `Trans-Move-Field@L14734`, `Trans-Move-Field-Partial@L14752`, `Trans-Partial-To-Moved@L14770`, `Trans-Reassign@L14788`, `Trans-Moved-NoAccess@L14806`, `Trans-Partial-NoAccess@L14824`
- `Trans-Let-NoReassign@L14842`, `def.BindingInfoJoin@L14860`, `def.BindingScopeJoin@L14876`, `def.BindingEnvironmentJoin@L14892`, `def.FieldHead@L14975`, `def.FieldPathOf@L14995`, `def.PlacePath@L15014`, `def.ArgumentPassExpression@L15094`
- `def.AccessStateOk@L15112`, `def.PartialMoveStateUpdate@L15128`, `def.ExpressionTypeLookupForAccess@L15144`, `def.AccessOk@L15159`, `def.BindingMovabilityOperator@L15175`, `def.MoveExpressionPredicate@L15190`, `def.InitializationResponsibility@L15205`, `def.BindingInitializerExpression@L15222`
- `def.BindingInitializerScope@L15238`, `def.TemporaryScope@L15254`, `def.TemporaryValuePredicate@L15270`, `def.TemporaryEvaluationOrder@L15284`, `def.ControlStatementExpression@L15305`, `def.TemporaryDropOrder@L15321`, `def.OptionalExpressionList@L15336`, `def.StatementExpressions@L15351`
- `def.StatementAndBindingScopes@L15378`, `def.BlockStatements@L15395`, `def.StatementBlocks@L15409`, `def.StatementSubExpressions@L15427`, `def.StatementSubStatements@L15443`, `def.SubBlocks@L15461`, `def.MapEntries@L15477`, `def.MapUnion@L15491`
- `def.IntroduceAllBindings@L15505`, `def.BindInfoMap@L15519`, `def.EffectiveMovability@L15533`, `def.BindingNames@L15568`, `def.JoinAllBindings@L15582`, `def.ConsumeOnMove@L15644`, `def.MoveExpressionInnerPlace@L15660`, `def.BindingJudgmentSet@L15674`
- `def.StaticBindingMaps@L15688`, `def.ProcedureEntryBindingScopes@L15706`, `def.ParameterBindingMap@L15721`, `def.MethodParameterBindingMap@L15736`, `def.ParameterTypeMap@L15750`, `def.ParameterMoveAndResponsibility@L15765`, `def.InitialBindingEnvironment@L15780`, `def.BindCheck@L15808`
- `def.ProcedureBindingCheck@L15822`, `def.MethodParametersForBinding@L15836`, `def.MethodBindingCheck@L15850`, `def.ClassMethodBindingCheck@L15864`, `def.StateMethodBindingCheck@L15878`, `def.TransitionBindingCheck@L15892`, `def.BindingDiagnosticReferences@L15906`, `req.FeatureSpecificBJudgmentOwnership@L15920`

#### `checker.permission-state`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L14536-L15740.

- `def.PermissionOfType@L14590`, `def.PermissionActivityDomain@L14605`, `def.PermissionEnvironment@L14619`, `def.PermissionScopeStackOps@L14635`, `def.PermissionLookup@L14650`, `def.PermissionUpdate@L14667`, `def.PermissionStateJoin@L14910`, `def.PermissionAtScope@L14925`
- `def.PermissionScopeJoin@L14941`, `def.PermissionEnvironmentJoin@L14955`, `def.AccessPathPrefixes@L15030`, `def.AccessPathOk@L15046`, `def.SuspendUniquePath@L15060`, `def.ReactivatePermissionKeys@L15079`, `def.JoinAllPermissions@L15598`, `def.PermissionTopScopeOps@L15614`
- `def.PermissionRoots@L15630`, `def.InitialPermissionEnvironment@L15794`

#### `checker.regions`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L15494-L16037.

- `def.RegionBindingInfo@L15548`, `def.RegionOptionsFields@L15938`, `def.RegionOptionsDecl@L15955`, `def.RegionPreallocation@L15970`, `def.RegionActiveType@L15985`, `def.FreshRegion@L15999`, `def.RegionOptionsExpression@L16013`, `def.RegionBind@L16028`
- `def.InnermostActiveRegion@L16044`, `def.FrameBind@L16061`, `req.RegionSyntheticIdentifierRestriction@L16077`, `req.FrameSyntheticIdentifierRestriction@L16091`

#### `checker.provenance`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L16053-L16840.

- `def.ProvenanceTags@L16107`, `def.RegionNesting@L16121`, `def.StrictProvenanceLifetimeOrder@L16135`, `def.ProvenanceLifetimeOrder@L16149`, `def.FrameTarget@L16163`, `def.FrameTargetProvenanceOrder@L16178`, `def.ProvenanceJoin@L16193`, `def.JoinAllProvenance@L16210`
- `def.ProvenanceEnvironmentShape@L16228`, `def.ProvenanceScopeAccessors@L16246`, `def.ProvenanceScopeStackOps@L16263`, `def.ProvenanceLookup@L16278`, `def.ProvenanceIntro@L16294`, `def.ProvenanceIntroAll@L16308`, `def.ParameterProvenanceInitialization@L16323`, `def.ProvenanceRegionEntryResolution@L16338`
- `def.ProvenanceRegionAliasIntro@L16357`, `def.FreshRegionTag@L16371`, `def.AllocationRegionTagSelection@L16385`, `def.FreshRegionExpression@L16403`, `def.ProvenanceJudgmentSets@L16417`, `def.CaseBodyProvenance@L16434`, `def.CasePatternProvenanceEnvironment@L16449`, `def.CaseProvenance@L16463`
- `def.CaseElseProvenance@L16477`, `rules.ProvenanceChecking@L16492`, `P-If-Is@L16510`, `P-If-Cases@L16528`, `def.ClosureCaptureProvenance@L16548`, `def.ClosureTargetProvenance@L16562`, `def.ClosureLocalSharedCaptures@L16578`, `def.ClosureEscapeCheck@L16592`
- `P-Closure-NonCapturing@L16608`, `P-Closure-Capturing@L16626`, `P-Closure-Escape-Err@L16645`, `def.FrameProvenance@L16664`, `def.BreakProvenance@L16682`, `def.IteratorElementProvenance@L16696`, `def.InfiniteLoopProvenance@L16710`, `def.FiniteLoopProvenance@L16725`
- `def.ExtendProvenanceForPattern@L16740`, `P-Loop-Infinite@L16754`, `P-Loop-Conditional@L16772`, `P-Loop-Iter@L16790`, `def.ProvenanceEscapeHelpers@L16808`, `req.NoGeneralHeapEscapeConversion@L16822`, `def.BindingProvenance@L16836`, `def.StaticBindingProvenance@L16852`
- `def.AssignmentProvenanceEscapeCheck@L16866`, `def.ProvenanceEscapeJudgmentSet@L16880`, `req.ProvenanceEscapeCheckPurpose@L16894`

#### `checker.types`

Count: 283 total; 181 required; 0 recommended; 0 informative. Ledger line span: L23734-L40932.

- `def.TypeEquivalenceJudgementSet@L23788`, `def.ConstLenJudgementSet@L23802`, `ConstLen-Lit@L23816`, `ConstLen-Path@L23834`, `ConstLen-Err@L23852`, `def.UnionMembersEquivalence@L23870`, `T-Equiv-Prim@L23884`, `T-Equiv-Perm@L23902`
- `T-Equiv-Tuple@L23920`, `T-Equiv-Array@L23938`, `T-Equiv-Slice@L23956`, `T-Equiv-Func@L23974`, `T-Equiv-Closure@L23992`, `T-Equiv-Union@L24010`, `T-Equiv-Path@L24028`, `T-Equiv-ModalState@L24046`
- `T-Equiv-String@L24064`, `T-Equiv-Bytes@L24082`, `T-Equiv-Range@L24100`, `T-Equiv-RangeInclusive@L24118`, `T-Equiv-RangeFrom@L24136`, `T-Equiv-RangeTo@L24154`, `T-Equiv-RangeToInclusive@L24172`, `T-Equiv-RangeFull@L24190`
- `T-Equiv-Ptr@L24208`, `T-Equiv-RawPtr@L24226`, `T-Equiv-Dynamic@L24244`, `T-Equiv-Apply@L24262`, `T-Equiv-Opaque@L24280`, `T-Equiv-Refine@L24298`, `def.PredicateEquivalence@L24316`, `T-Equiv-Refine-Norm@L24330`
- `T-Equiv-Refl@L24348`, `T-Equiv-Sym@L24366`, `T-Equiv-Trans@L24384`, `def.SubtypingJudgementSet@L24405`, `req.NoIntegerNumericSubtyping@L24419`, `req.NoFloatNumericSubtyping@L24433`, `req.PermissionAdmissibilityOwnedByChapter10@L24447`, `Sub-Perm@L24461`
- `Sub-Never@L24479`, `Sub-Tuple@L24497`, `Sub-Array@L24515`, `Sub-Slice@L24533`, `Sub-Range@L24551`, `Sub-RangeInclusive@L24569`, `Sub-RangeFrom@L24587`, `Sub-RangeTo@L24605`
- `Sub-RangeToInclusive@L24623`, `Sub-RangeFull@L24641`, `Sub-Ptr-State@L24659`, `Sub-Modal-Niche@L24677`, `Sub-Func@L24695`, `Sub-Closure@L24713`, `Sub-Async@L24731`, `def.UnionMember@L40418`
- `Sub-Member-Union@L40432`, `Sub-Union-Width@L40450`, `def.VarianceDomain@L24800`, `def.VarianceOfSignature@L24814`, `def.VarianceOf@L24828`, `def.VarianceSatisfied@L24842`, `Sub-Generic@L24860`, `Sub-Generic-Invariant-Err@L24878`
- `Sub-RangeToInclusive@L24623`, `Sub-RangeFull@L24641`, `Sub-Ptr-State@L24659`, `Sub-Modal-Niche@L24677`, `Sub-Func@L24695`, `Sub-Closure@L24713`, `Sub-Async@L24731`, `def.UnionMember@L40418`
- `Sub-Member-Union@L40432`, `Sub-Union-Width@L40450`, `def.VarianceDomain@L24800`, `def.VarianceOfSignature@L24814`, `def.VarianceOf@L24828`, `def.VarianceSatisfied@L24842`, `Sub-Generic@L24860`, `Sub-Generic-Invariant-Err@L24878`
- `Sub-Generic-Covariant-Err@L24896`, `Sub-Generic-Contravariant-Err@L24914`, `Sub-Refl@L24932`, `Sub-Trans@L24950`, `def.TypeInferenceJudgementSet@L24971`, `def.TypeEqualityConstraint@L24985`, `def.TypeEqualityConstraintSet@L24999`, `req.ConstraintGenerationFeatureLocal@L25013`
- `def.TypeVariableDomain@L25027`, `def.TypeVariablesOfType@L25041`, `def.SubstitutionDomain@L25055`, `def.SubstitutionDefinedDomain@L25069`, `def.IdentitySubstitution@L25083`, `def.SubstitutionApplication@L25097`, `def.SubstitutionComposition@L25121`, `def.UnificationStateDomain@L25135`
- `Unify-Empty@L25149`, `Unify-Eq@L25166`, `Unify-Var-L@L25184`, `Unify-Var-R@L25202`, `Unify-Occurs-Fail@L25220`, `Unify-Tuple@L25238`, `Unify-Tuple-Fail@L25256`, `Unify-Array@L25274`
- `Unify-Array-Len-Fail@L25292`, `Unify-Slice@L25310`, `Unify-Perm@L25328`, `Unify-Perm-Fail@L25346`, `Unify-Func@L25364`, `Unify-Func-Fail@L25383`, `Unify-Closure@L25401`, `Unify-Closure-Fail@L25419`
- `Unify-Ptr@L25437`, `Unify-Ptr-State-Fail@L25455`, `Unify-RawPtr@L25473`, `Unify-RawPtr-Qual-Fail@L25491`, `Unify-Apply@L25509`, `Unify-Apply-Fail@L25527`, `Unify-Range@L25545`, `Unify-RangeInclusive@L25563`
- `Unify-RangeFrom@L25581`, `Unify-RangeTo@L25599`, `Unify-RangeToInclusive@L25617`, `Unify-Refine@L25635`, `Unify-Refine-Pred-Fail@L25653`, `Unify-Prim-Fail@L25671`, `Unify-Rigid-Fail@L25689`, `Unify-Ctor-Mismatch@L25714`
- `Unify-Ok@L25732`, `Unify-Err@L25750`, `Solve-Unify@L25768`, `Solve-Fail@L25786`, `Syn-Expr@L25804`, `Syn-Ident@L25822`, `Syn-Unit@L25840`, `Syn-Tuple@L25857`
- `Syn-Call@L25875`, `Syn-Call-Err@L25893`, `Chk-Subsumption-Modal-NonNiche@L25911`, `Chk-Subsumption@L25929`, `Chk-Null-Ptr@L25947`, `def.PtrNullExpectedType@L25965`, `Syn-PtrNull-Err@L25979`, `Chk-PtrNull-Err@L25997`
- `req.FeatureLocalSynthesisAndCheckingOwnership@L26015`, `property.TypeSystemMetatheory.Intro@L26032`, `Progress@L26046`, `Preservation@L26065`, `No-Use-After-Free@L26081`, `No-Double-Free@L26097`, `No-Dangling-Pointers@L26113`, `Exclusivity-Invariant@L26129`
- `Permission-Preservation@L26145`, `State-Determinism@L26161`, `No-Resurrection@L26177`, `Data-Race-Freedom@L26193`, `Fork-Join-Guarantee@L26209`, `Key-Serialization@L26225`, `Async-Key-Safety@L26241`, `req.PermissionQualifiedSubtypingPermissionEquality@L29410`
- `Parse-Record@L38217`, `Parse-RecordBody@L38235`, `Parse-RecordMemberList-End@L38253`, `Parse-RecordMemberList-Cons@L38271`, `Parse-RecordMember-Method@L38289`, `Parse-RecordMember-AssociatedType@L38307`, `Parse-RecordMember-Field@L38325`, `Parse-RecordFieldDeclAfterVis@L38343`
- `Parse-RecordFieldInitOpt-None@L38361`, `Parse-RecordFieldInitOpt-Yes@L38379`, `Parse-Record-Literal@L38397`, `def.RecordDeclAst@L38415`, `def.RecordMemberAst@L38432`, `def.RecordExprAst@L38450`, `def.RecordMembersSelectors@L38464`, `def.RecordPath@L38479`
- `Bind-Record@L38497`, `Resolve-RecordPath@L38514`, `ResolveQual-Name-Record@L38532`, `ResolveQual-Apply-RecordLit@L38550`, `ResolveItem-Record@L38568`, `def.RecordFieldInitOk@L38586`, `def.RecordFieldVisibility@L38600`, `WF-Record@L38615`
- `WF-Record-DupField@L38633`, `WF-RecordDecl@L38651`, `FieldVisOk-Err@L38669`, `def.RecordDefaultConstructible@L38687`, `def.RecordCallee@L38701`, `T-Record-Default@L38715`, `def.RecordFieldNameSets@L38733`, `def.RecordFieldLookup@L38751`
- `T-Record-Literal@L38767`, `EvalSigma-Record@L38801`, `EvalSigma-Record-Ctrl@L38819`, `ApplyRecordCtorSigma@L38851`, `ApplyRecordCtorSigma-Ctrl@L38869`, `Layout-Record-Empty@L38910`, `Layout-Record-Cons@L38927`, `Size-Record@L38945`
- `Align-Record@L38963`, `Layout-Record@L38981`, `LowerFieldInits-Empty@L39069`, `LowerFieldInits-Cons@L39086`, `Lower-Expr-Record@L39104`, `Lower-CallIR-RecordCtor@L39122`, `diagnostics.Records@L39140`, `Parse-Enum@L39207`
- `Parse-EnumBody@L39225`, `Parse-VariantMembers-Empty@L39243`, `Parse-VariantMembers-Cons@L39261`, `Parse-VariantSep-End@L39279`, `Parse-VariantSep-Terminator@L39297`, `Parse-Variant@L39315`, `Parse-VariantPayloadOpt-None@L39333`, `Parse-VariantPayloadOpt-Tuple@L39351`
- `Parse-VariantPayloadOpt-Record@L39369`, `Parse-VariantDiscriminantOpt-None@L39387`, `Parse-VariantDiscriminantOpt-Yes@L39405`, `req.EnumLiteralResolutionOwnership@L39437`, `def.EnumDeclAst@L39451`, `def.VariantDeclAst@L39468`, `def.EnumVariantHelpers@L39484`, `Bind-Enum@L39506`
- `def.EnumPayloadWellFormedness@L39523`, `Resolve-EnumUnit@L39541`, `Resolve-EnumTuple@L39559`, `Resolve-EnumRecord@L39577`, `ResolveQual-Name-Enum@L39595`, `ResolveQual-Apply-Enum-Tuple@L39613`, `ResolveQual-Apply-Enum-Record@L39631`, `ResolveItem-Enum@L39649`
- `def.EnumDiscriminantSequence@L39667`, `Enum-Disc-NotInt@L39689`, `Enum-Disc-Invalid@L39707`, `Enum-Disc-Negative@L39725`, `Enum-Disc-Dup@L39743`, `Enum-Empty-Err@L39761`, `Enum-Variant-Dup@L39779`, `def.EnumDiscriminantType@L39797`
- `WF-EnumDecl@L39816`, `def.EnumLiteralPayloadHelpers@L39834`, `T-Enum-Lit-Unit@L39852`, `Enum-Lit-Unknown@L39870`, `T-Enum-Lit-Tuple@L39888`, `Enum-Lit-Tuple-Arity-Err@L39906`, `T-Enum-Lit-Record@L39924`, `Enum-Lit-Record-MissingField@L39942`
- `EvalSigma-Enum-Unit@L39976`, `EvalSigma-Enum-Tuple@L39993`, `EvalSigma-Enum-Tuple-Ctrl@L40011`, `EvalSigma-Enum-Record@L40029`, `EvalSigma-Enum-Record-Ctrl@L40047`, `Layout-Enum-Tagged@L40094`, `Size-Enum@L40112`, `Align-Enum@L40130`
- `Layout-Enum@L40148`, `Lower-Expr-Enum-Unit@L40196`, `Lower-Expr-Enum-Tuple@L40213`, `Lower-Expr-Enum-Record@L40231`, `diagnostics.Enums@L40249`, `req.UnionIntroductionSemantic@L40296`, `Parse-UnionTail-None@L40312`, `Parse-UnionTail-Cons@L40330`
- `def.TypeUnionAst@L40348`, `def.UnionMemberSets@L40364`, `WF-Union@L40382`, `WF-Union-TooFew@L40400`, `def.UnionMember@L40418`, `Sub-Member-Union@L40432`, `Sub-Union-Width@L40450`, `T-Union-Intro@L40468`
- `Union-DirectAccess-Err@L40486`, `req.UnionMatchingPropagationOwnership@L40504`, `Layout-Union-Niche@L40691`, `Layout-Union-Tagged@L40709`, `Size-Union@L40727`, `Align-Union@L40745`, `Layout-Union@L40763`, `req.UnionDiagnosticOwnership@L40881`
- `Parse-Type-Alias@L40920`, `def.TypeAliasDeclAst@L40938`, `def.TypeAliasAccessors@L40954`, `Bind-TypeAlias@L40972`, `ResolveItem-TypeAlias@L40989`, `def.AliasNormalization@L41007`, `def.AliasPathNormalization@L41043`, `def.AliasTransparent@L41060`
- `def.AliasGraph@L41074`, `def.TypePaths@L41088`, `def.TypePathsOfModalRef@L41124`, `def.AliasCycle@L41139`, `TypeAlias-Ok@L41153`, `TypeAlias-Recursive-Err@L41170`, `req.TypeAliasDynamicSemantics@L41187`, `Size-Alias@L41205`
- `Align-Alias@L41223`, `Layout-Alias@L41241`, `req.TypeAliasDiagnosticOwnership@L41273`
- `Permission-Preservation@L26145`, `State-Determinism@L26161`, `No-Resurrection@L26177`, `Data-Race-Freedom@L26193`, `Fork-Join-Guarantee@L26209`, `Key-Serialization@L26225`, `Async-Key-Safety@L26241`, `req.PermissionQualifiedSubtypingPermissionEquality@L29410`
- `Parse-Record@L38217`, `Parse-RecordBody@L38235`, `Parse-RecordMemberList-End@L38253`, `Parse-RecordMemberList-Cons@L38271`, `Parse-RecordMember-Method@L38289`, `Parse-RecordMember-AssociatedType@L38307`, `Parse-RecordMember-Field@L38325`, `Parse-RecordFieldDeclAfterVis@L38343`
- `Parse-RecordFieldInitOpt-None@L38361`, `Parse-RecordFieldInitOpt-Yes@L38379`, `Parse-Record-Literal@L38397`, `def.RecordDeclAst@L38415`, `def.RecordMemberAst@L38432`, `def.RecordExprAst@L38450`, `def.RecordMembersSelectors@L38464`, `def.RecordPath@L38479`
- `Bind-Record@L38497`, `Resolve-RecordPath@L38514`, `ResolveQual-Name-Record@L38532`, `ResolveQual-Apply-RecordLit@L38550`, `ResolveItem-Record@L38568`, `def.RecordFieldInitOk@L38586`, `def.RecordFieldVisibility@L38600`, `WF-Record@L38615`
- `WF-Record-DupField@L38633`, `WF-RecordDecl@L38651`, `FieldVisOk-Err@L38669`, `def.RecordDefaultConstructible@L38687`, `def.RecordCallee@L38701`, `T-Record-Default@L38715`, `def.RecordFieldNameSets@L38733`, `def.RecordFieldLookup@L38751`
- `T-Record-Literal@L38767`, `EvalSigma-Record@L38801`, `EvalSigma-Record-Ctrl@L38819`, `ApplyRecordCtorSigma@L38851`, `ApplyRecordCtorSigma-Ctrl@L38869`, `Layout-Record-Empty@L38910`, `Layout-Record-Cons@L38927`, `Size-Record@L38945`
- `Align-Record@L38963`, `Layout-Record@L38981`, `LowerFieldInits-Empty@L39069`, `LowerFieldInits-Cons@L39086`, `Lower-Expr-Record@L39104`, `Lower-CallIR-RecordCtor@L39122`, `diagnostics.Records@L39140`, `Parse-Enum@L39207`
- `Parse-EnumBody@L39225`, `Parse-VariantMembers-Empty@L39243`, `Parse-VariantMembers-Cons@L39261`, `Parse-VariantSep-End@L39279`, `Parse-VariantSep-Terminator@L39297`, `Parse-Variant@L39315`, `Parse-VariantPayloadOpt-None@L39333`, `Parse-VariantPayloadOpt-Tuple@L39351`
- `Parse-VariantPayloadOpt-Record@L39369`, `Parse-VariantDiscriminantOpt-None@L39387`, `Parse-VariantDiscriminantOpt-Yes@L39405`, `req.EnumLiteralResolutionOwnership@L39437`, `def.EnumDeclAst@L39451`, `def.VariantDeclAst@L39468`, `def.EnumVariantHelpers@L39484`, `Bind-Enum@L39506`
- `def.EnumPayloadWellFormedness@L39523`, `Resolve-EnumUnit@L39541`, `Resolve-EnumTuple@L39559`, `Resolve-EnumRecord@L39577`, `ResolveQual-Name-Enum@L39595`, `ResolveQual-Apply-Enum-Tuple@L39613`, `ResolveQual-Apply-Enum-Record@L39631`, `ResolveItem-Enum@L39649`
- `def.EnumDiscriminantSequence@L39667`, `Enum-Disc-NotInt@L39689`, `Enum-Disc-Invalid@L39707`, `Enum-Disc-Negative@L39725`, `Enum-Disc-Dup@L39743`, `Enum-Empty-Err@L39761`, `Enum-Variant-Dup@L39779`, `def.EnumDiscriminantType@L39797`
- `WF-EnumDecl@L39816`, `def.EnumLiteralPayloadHelpers@L39834`, `T-Enum-Lit-Unit@L39852`, `Enum-Lit-Unknown@L39870`, `T-Enum-Lit-Tuple@L39888`, `Enum-Lit-Tuple-Arity-Err@L39906`, `T-Enum-Lit-Record@L39924`, `Enum-Lit-Record-MissingField@L39942`
- `EvalSigma-Enum-Unit@L39976`, `EvalSigma-Enum-Tuple@L39993`, `EvalSigma-Enum-Tuple-Ctrl@L40011`, `EvalSigma-Enum-Record@L40029`, `EvalSigma-Enum-Record-Ctrl@L40047`, `Layout-Enum-Tagged@L40094`, `Size-Enum@L40112`, `Align-Enum@L40130`
- `Layout-Enum@L40148`, `Lower-Expr-Enum-Unit@L40196`, `Lower-Expr-Enum-Tuple@L40213`, `Lower-Expr-Enum-Record@L40231`, `diagnostics.Enums@L40249`, `req.UnionIntroductionSemantic@L40296`, `Parse-UnionTail-None@L40312`, `Parse-UnionTail-Cons@L40330`
- `def.TypeUnionAst@L40348`, `def.UnionMemberSets@L40364`, `WF-Union@L40382`, `WF-Union-TooFew@L40400`, `def.UnionMember@L40418`, `Sub-Member-Union@L40432`, `Sub-Union-Width@L40450`, `T-Union-Intro@L40468`
- `Union-DirectAccess-Err@L40486`, `req.UnionMatchingPropagationOwnership@L40504`, `Layout-Union-Niche@L40691`, `Layout-Union-Tagged@L40709`, `Size-Union@L40727`, `Align-Union@L40745`, `Layout-Union@L40763`, `req.UnionDiagnosticOwnership@L40881`
- `Parse-Type-Alias@L40920`, `def.TypeAliasDeclAst@L40938`, `def.TypeAliasAccessors@L40954`, `Bind-TypeAlias@L40972`, `ResolveItem-TypeAlias@L40989`, `def.AliasNormalization@L41007`, `def.AliasPathNormalization@L41043`, `def.AliasTransparent@L41060`
- `def.AliasGraph@L41074`, `def.TypePaths@L41088`, `def.TypePathsOfModalRef@L41124`, `def.AliasCycle@L41139`, `TypeAlias-Ok@L41153`, `TypeAlias-Recursive-Err@L41170`, `req.TypeAliasDynamicSemantics@L41187`, `Size-Alias@L41205`
- `Align-Alias@L41223`, `Layout-Alias@L41241`, `req.TypeAliasDiagnosticOwnership@L41273`

#### `checker.attributes`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L26829-L27272.

- `req.MalformedAttributeSyntaxIllFormed@L26883`, `def.AttributeTargetDomain@L26897`, `def.AttributeRegistry@L26911`, `def.VendorAttributeRegistryInitial@L26925`, `def.SpecAttributeRegistry@L26939`, `def.SpecAttributeTargets@L26962`, `def.AttributeListWellFormedJudgementSet@L26998`, `AttrList-Ok@L27012`
- `AttrList-Unknown@L27030`, `AttrList-Target-Err@L27048`, `def.AttributeStaticSemantics.Helpers2@L27066`, `req.MemoryOrderAttributeTargets@L27080`, `req.AttributeListWellFormednessCheck@L27094`, `req.MultipleAttributeListConcatenation@L27108`, `req.FfiAttributeOwnership@L27122`, `conformance.VendorAttributeStaticSemantics@L27328`
- `req.MalformedAttributeSyntaxIllFormed@L26883`, `def.AttributeTargetDomain@L26897`, `def.AttributeRegistry@L26911`, `def.VendorAttributeRegistryInitial@L26925`, `def.SpecAttributeRegistry@L26939`, `def.SpecAttributeTargets@L26962`, `def.AttributeListWellFormedJudgementSet@L26998`, `AttrList-Ok@L27012`
- `AttrList-Unknown@L27030`, `AttrList-Target-Err@L27048`, `def.AttributeStaticSemantics.Helpers2@L27066`, `req.MemoryOrderAttributeTargets@L27080`, `req.AttributeListWellFormednessCheck@L27094`, `req.MultipleAttributeListConcatenation@L27108`, `req.FfiAttributeOwnership@L27122`, `conformance.VendorAttributeStaticSemantics@L27328`

#### `checker.attributes.layout`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L27401-L27559.

- `req.LayoutCRecordSemantics@L27457`, `req.LayoutCEnumSemantics@L27474`, `req.LayoutExplicitEnumDiscriminant@L27491`, `req.LayoutPackedRecordSemantics@L27510`, `req.PackedFieldReferenceRequiresUnsafe@L27527`, `req.LayoutAlignSemantics@L27543`, `def.ValidLayoutAttributeCombinations@L27562`, `def.InvalidLayoutAttributeCombinations@L27582`
- `def.LayoutAttributeApplicability@L27597`, `req.LayoutAttributeConstraints@L27615`
- `req.LayoutCRecordSemantics@L27457`, `req.LayoutCEnumSemantics@L27474`, `req.LayoutExplicitEnumDiscriminant@L27491`, `req.LayoutPackedRecordSemantics@L27510`, `req.PackedFieldReferenceRequiresUnsafe@L27527`, `req.LayoutAlignSemantics@L27543`, `def.ValidLayoutAttributeCombinations@L27562`, `def.InvalidLayoutAttributeCombinations@L27582`
- `def.LayoutAttributeApplicability@L27597`, `req.LayoutAttributeConstraints@L27615`

#### `checker.attributes.optimization`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27688-L27708.

- `req.InlineAttributeSemantics@L27744`, `req.ColdAttributeSemantics@L27764`
- `req.InlineAttributeSemantics@L27744`, `req.ColdAttributeSemantics@L27764`

#### `checker.attributes.metadata`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L27837-L28187.

- `def.DynamicDeclarationPredicate@L27893`, `def.DynamicExpressionPredicate@L27907`, `def.DynamicScopePredicate@L27921`, `def.InDynamicContext@L27935`, `req.DeprecatedAttributeSemantics@L27951`, `req.DynamicAttributeSemantics@L27965`, `req.DynamicScopeDetermination@L27979`, `def.ComputeDynamicContext@L27995`
- `def.FindInnermostDynamic@L28018`, `def.MinimalSpan@L28039`, `DynamicContext-Override@L28057`, `req.DynamicContextOverridePropagation@L28077`, `DynamicContext-NoInherit-Call@L28091`, `req.DynamicContextLexicalNoCallPropagation@L28111`, `req.DynamicEffectsAndRestrictions@L28125`, `req.DynamicTargetRestrictions@L28142`
- `req.EmptyDynamicScopeWarning@L28159`, `req.StaleOkAttributeSemantics@L28173`, `req.VerificationModeAttributeSemantics@L28187`, `req.ReflectAttributeSemantics@L28201`, `req.DeriveAttributeSemantics@L28215`, `req.EmitAttributeSemantics@L28229`, `req.FilesAttributeSemantics@L28243`
- `def.DynamicDeclarationPredicate@L27893`, `def.DynamicExpressionPredicate@L27907`, `def.DynamicScopePredicate@L27921`, `def.InDynamicContext@L27935`, `req.DeprecatedAttributeSemantics@L27951`, `req.DynamicAttributeSemantics@L27965`, `req.DynamicScopeDetermination@L27979`, `def.ComputeDynamicContext@L27995`
- `def.FindInnermostDynamic@L28018`, `def.MinimalSpan@L28039`, `DynamicContext-Override@L28057`, `req.DynamicContextOverridePropagation@L28077`, `DynamicContext-NoInherit-Call@L28091`, `req.DynamicContextLexicalNoCallPropagation@L28111`, `req.DynamicEffectsAndRestrictions@L28125`, `req.DynamicTargetRestrictions@L28142`
- `req.EmptyDynamicScopeWarning@L28159`, `req.StaleOkAttributeSemantics@L28173`, `req.VerificationModeAttributeSemantics@L28187`, `req.ReflectAttributeSemantics@L28201`, `req.DeriveAttributeSemantics@L28215`, `req.EmitAttributeSemantics@L28229`, `req.FilesAttributeSemantics@L28243`

#### `checker.permissions`

Count: 32 total; 32 required; 0 recommended; 0 informative. Ledger line span: L28361-L29129.

- `def.PermissionQualifierSemantics@L28702`, `req.PermissionRegimesDistinct@L28716`, `def.PermissionRegimeProperties@L28730`, `req.PermissionRegimeConstraints@L28750`, `def.SharedPermissionOperationMatrix@L28768`, `Layout-Perm@L28826`, `SizeOf-Perm@L28842`, `AlignOf-Perm@L28858`
- `conformance.AliasAndExclusivityRules@L28907`, `def.AliasingByOverlappingStorage@L28959`, `req.UniqueExclusivityInvariant@L28975`, `def.PermissionCoexistenceMatrix@L28991`, `req.BindingActivityNoConcreteSyntax@L29059`, `def.UniqueBindingActivityStates@L29109`, `Inactive-Enter@L29128`, `Inactive-Exit@L29146`
- `req.InactiveUniqueBindingNoDirectUse@L29164`, `req.BindingActivityNoAliasCreation@L29180`, `req.BindingActivityDeterministicReactivation@L29194`, `conformance.BindingActivityLowering@L29208`, `req.BindingActivityDiagnosticOwnership@L29224`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29243`, `def.PermissionAdmissibilityAstInputs@L29275`, `req.PermissionAdmissibilityScope@L29294`
- `def.PermAdmitsJudgementSet@L29310`, `def.PermissionAdmissibilityPairs@L29324`, `req.PermAdmitsUseSitesNoTypeRewrite@L29344`, `def.MethodReceiverPermissionAdmissibility@L29358`, `def.MethodReceiverPermissionMatrix@L29376`, `req.PermissionAdmissibilityNoImplicitConversion@L29394`, `conformance.PermissionAdmissibilitySharedKeyGate@L29440`, `diagnostics.PermissionAdmissibility@L29470`
- `def.PermissionQualifierSemantics@L28702`, `req.PermissionRegimesDistinct@L28716`, `def.PermissionRegimeProperties@L28730`, `req.PermissionRegimeConstraints@L28750`, `def.SharedPermissionOperationMatrix@L28768`, `Layout-Perm@L28826`, `SizeOf-Perm@L28842`, `AlignOf-Perm@L28858`
- `conformance.AliasAndExclusivityRules@L28907`, `def.AliasingByOverlappingStorage@L28959`, `req.UniqueExclusivityInvariant@L28975`, `def.PermissionCoexistenceMatrix@L28991`, `req.BindingActivityNoConcreteSyntax@L29059`, `def.UniqueBindingActivityStates@L29109`, `Inactive-Enter@L29128`, `Inactive-Exit@L29146`
- `req.InactiveUniqueBindingNoDirectUse@L29164`, `req.BindingActivityNoAliasCreation@L29180`, `req.BindingActivityDeterministicReactivation@L29194`, `conformance.BindingActivityLowering@L29208`, `req.BindingActivityDiagnosticOwnership@L29224`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29243`, `def.PermissionAdmissibilityAstInputs@L29275`, `req.PermissionAdmissibilityScope@L29294`
- `def.PermAdmitsJudgementSet@L29310`, `def.PermissionAdmissibilityPairs@L29324`, `req.PermAdmitsUseSitesNoTypeRewrite@L29344`, `def.MethodReceiverPermissionAdmissibility@L29358`, `def.MethodReceiverPermissionMatrix@L29376`, `req.PermissionAdmissibilityNoImplicitConversion@L29394`, `conformance.PermissionAdmissibilitySharedKeyGate@L29440`, `diagnostics.PermissionAdmissibility@L29470`

#### `checker.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L31062-L31062.

- `req.ExternBlockDiagnosticOwnership@L31403`
- `req.ExternBlockDiagnosticOwnership@L31403`

#### `checker.types.primitive`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L34345-L34624.

- `Parse-Prim-Type@L34686`, `Parse-Unit-Type@L34704`, `Parse-Never-Type@L34722`, `def.PrimitiveTypeName@L34740`, `def.TypePrimAst@L34756`, `def.NumericPrimitiveTypeSets@L34770`, `def.TypeWFJudgementSet@L34788`, `WF-Prim@L34804`
- `def.PrimitiveFloatRepresentation@L34822`, `def.DefaultNumericTypes@L34845`, `def.PrimitiveIntegerWidths@L34860`, `def.PrimitiveRangeOf@L34876`, `req.PrimitiveTypeDiagnosticOwnership@L34965`
- `Parse-Prim-Type@L34686`, `Parse-Unit-Type@L34704`, `Parse-Never-Type@L34722`, `def.PrimitiveTypeName@L34740`, `def.TypePrimAst@L34756`, `def.NumericPrimitiveTypeSets@L34770`, `def.TypeWFJudgementSet@L34788`, `WF-Prim@L34804`
- `def.PrimitiveFloatRepresentation@L34822`, `def.DefaultNumericTypes@L34845`, `def.PrimitiveIntegerWidths@L34860`, `def.PrimitiveRangeOf@L34876`, `req.PrimitiveTypeDiagnosticOwnership@L34965`

#### `checker.types.tuples`

Count: 41 total; 41 required; 0 recommended; 0 informative. Ledger line span: L34683-L35482.

- `Parse-Tuple-Type@L35024`, `Parse-TupleTypeElems-Empty@L35042`, `Parse-TupleTypeElems-One@L35060`, `Parse-TupleTypeElems-Many@L35078`, `def.TupleScanDelimiterDeltas@L35096`, `TupleScan-EOF@L35154`, `TupleScan-EndParen@L35172`, `TupleScan-SingletonComma@L35189`
- `TupleScan-Separator@L35206`, `TupleScan-Advance@L35223`, `def.TupleParen@L35240`, `Parse-Tuple-Literal@L35254`, `Parse-TupleExprElems-Empty@L35272`, `Parse-TupleExprElems-Single@L35290`, `Parse-TupleExprElems-Many@L35308`, `Postfix-TupleIndex@L35326`
- `def.TupleTypeAst@L35344`, `def.TupleExpressionAst@L35360`, `WF-Tuple@L35391`, `T-Tuple-Unit@L35409`, `T-Tuple@L35426`, `T-Tuple-Index@L35444`, `T-Tuple-Index-Perm@L35462`, `P-Tuple-Index@L35480`
- `P-Tuple-Index-Perm@L35498`, `def.ConstTupleIndex@L35516`, `TupleIndex-NonConst@L35530`, `TupleIndex-OOB@L35548`, `TupleAccess-NotTuple@L35566`, `req.TuplePatternRulesOwnership@L35584`, `EvalSigma-Tuple@L35614`, `EvalSigma-Tuple-Ctrl@L35632`
- `EvalSigma-TupleAccess@L35650`, `EvalSigma-TupleAccess-Ctrl@L35668`, `Layout-Tuple-Empty@L35702`, `Layout-Tuple-Cons@L35719`, `Size-Tuple@L35737`, `Align-Tuple@L35755`, `Layout-Tuple@L35773`, `Lower-Expr-Tuple@L35805`
- `diagnostics.Tuples@L35823`
- `Parse-Tuple-Type@L35024`, `Parse-TupleTypeElems-Empty@L35042`, `Parse-TupleTypeElems-One@L35060`, `Parse-TupleTypeElems-Many@L35078`, `def.TupleScanDelimiterDeltas@L35096`, `TupleScan-EOF@L35154`, `TupleScan-EndParen@L35172`, `TupleScan-SingletonComma@L35189`
- `TupleScan-Separator@L35206`, `TupleScan-Advance@L35223`, `def.TupleParen@L35240`, `Parse-Tuple-Literal@L35254`, `Parse-TupleExprElems-Empty@L35272`, `Parse-TupleExprElems-Single@L35290`, `Parse-TupleExprElems-Many@L35308`, `Postfix-TupleIndex@L35326`
- `def.TupleTypeAst@L35344`, `def.TupleExpressionAst@L35360`, `WF-Tuple@L35391`, `T-Tuple-Unit@L35409`, `T-Tuple@L35426`, `T-Tuple-Index@L35444`, `T-Tuple-Index-Perm@L35462`, `P-Tuple-Index@L35480`
- `P-Tuple-Index-Perm@L35498`, `def.ConstTupleIndex@L35516`, `TupleIndex-NonConst@L35530`, `TupleIndex-OOB@L35548`, `TupleAccess-NotTuple@L35566`, `req.TuplePatternRulesOwnership@L35584`, `EvalSigma-Tuple@L35614`, `EvalSigma-Tuple-Ctrl@L35632`
- `EvalSigma-TupleAccess@L35650`, `EvalSigma-TupleAccess-Ctrl@L35668`, `Layout-Tuple-Empty@L35702`, `Layout-Tuple-Cons@L35719`, `Size-Tuple@L35737`, `Align-Tuple@L35755`, `Layout-Tuple@L35773`, `Lower-Expr-Tuple@L35805`
- `diagnostics.Tuples@L35823`

#### `checker.types.arrays`

Count: 34 total; 34 required; 0 recommended; 0 informative. Ledger line span: L35530-L36185.

- `Parse-Array-Type@L35871`, `Parse-Array-Segment-Elem@L35889`, `Parse-Array-Segment-Repeat@L35907`, `Parse-Array-Segment-List-Empty@L35925`, `Parse-Array-Segment-List-Single@L35942`, `Parse-Array-Segment-List-Comma@L35960`, `Parse-Array-Literal@L35978`, `Postfix-Index@L35996`
- `def.ArrayAstForms@L36014`, `req.IndexAccessArrayOwnership@L36032`, `def.ConstIndex@L36046`, `WF-Array@L36062`, `def.ArraySegmentLength@L36080`, `T-Array-Literal-Segments@L36095`, `T-Index-Array@L36121`, `T-Index-Array-Dynamic@L36139`
- `T-Index-Array-Perm@L36157`, `T-Index-Array-Perm-Dynamic@L36175`, `P-Index-Array@L36193`, `P-Index-Array-Perm@L36211`, `P-Index-Array-Dynamic@L36229`, `P-Index-Array-Perm-Dynamic@L36247`, `Index-Array-NonConst-Err@L36265`, `Index-Array-OOB-Err@L36283`
- `Index-Array-NonUsize@L36301`, `EvalSigma-Array@L36352`, `EvalSigma-Array-Ctrl@L36370`, `EvalSigma-Index@L36388`, `EvalSigma-Index-OOB@L36406`, `Size-Array@L36426`, `Align-Array@L36444`, `Layout-Array@L36462`
- `Lower-Expr-Array@L36508`, `req.ArrayDiagnosticOwnership@L36526`
- `Parse-Array-Type@L35871`, `Parse-Array-Segment-Elem@L35889`, `Parse-Array-Segment-Repeat@L35907`, `Parse-Array-Segment-List-Empty@L35925`, `Parse-Array-Segment-List-Single@L35942`, `Parse-Array-Segment-List-Comma@L35960`, `Parse-Array-Literal@L35978`, `Postfix-Index@L35996`
- `def.ArrayAstForms@L36014`, `req.IndexAccessArrayOwnership@L36032`, `def.ConstIndex@L36046`, `WF-Array@L36062`, `def.ArraySegmentLength@L36080`, `T-Array-Literal-Segments@L36095`, `T-Index-Array@L36121`, `T-Index-Array-Dynamic@L36139`
- `T-Index-Array-Perm@L36157`, `T-Index-Array-Perm-Dynamic@L36175`, `P-Index-Array@L36193`, `P-Index-Array-Perm@L36211`, `P-Index-Array-Dynamic@L36229`, `P-Index-Array-Perm-Dynamic@L36247`, `Index-Array-NonConst-Err@L36265`, `Index-Array-OOB-Err@L36283`
- `Index-Array-NonUsize@L36301`, `EvalSigma-Array@L36352`, `EvalSigma-Array-Ctrl@L36370`, `EvalSigma-Index@L36388`, `EvalSigma-Index-OOB@L36406`, `Size-Array@L36426`, `Align-Array@L36444`, `Layout-Array@L36462`
- `Lower-Expr-Array@L36508`, `req.ArrayDiagnosticOwnership@L36526`

#### `checker.types.slices`

Count: 28 total; 28 required; 0 recommended; 0 informative. Ledger line span: L36224-L36756.

- `req.ArrayToSliceCoercionSemantic@L36565`, `Parse-Slice-Type@L36581`, `req.IndexAccessSliceOwnership@L36599`, `def.TypeSliceAst@L36613`, `req.IndexAccessSliceExpressionSemantics@L36629`, `def.RangeIndexType@L36643`, `WF-Slice@L36659`, `T-Index-Slice@L36677`
- `T-Index-Slice-Perm@L36695`, `T-Slice-From-Array@L36713`, `T-Slice-From-Array-Perm@L36731`, `T-Slice-From-Slice@L36749`, `T-Slice-From-Slice-Perm@L36767`, `P-Index-Slice@L36785`, `P-Index-Slice-Perm@L36803`, `P-Slice-From-Array@L36821`
- `P-Slice-From-Array-Perm@L36839`, `P-Slice-From-Slice@L36857`, `P-Slice-From-Slice-Perm@L36875`, `Coerce-Array-Slice@L36893`, `Index-NonIndexable@L36911`, `EvalSigma-Index-Range@L36960`, `EvalSigma-Index-Range-OOB@L36978`, `Size-Slice@L36998`
- `Align-Slice@L37015`, `Layout-Slice@L37032`, `Index-Slice-NonUsize@L37079`, `req.SliceDiagnosticOwnership@L37097`
- `req.ArrayToSliceCoercionSemantic@L36565`, `Parse-Slice-Type@L36581`, `req.IndexAccessSliceOwnership@L36599`, `def.TypeSliceAst@L36613`, `req.IndexAccessSliceExpressionSemantics@L36629`, `def.RangeIndexType@L36643`, `WF-Slice@L36659`, `T-Index-Slice@L36677`
- `T-Index-Slice-Perm@L36695`, `T-Slice-From-Array@L36713`, `T-Slice-From-Array-Perm@L36731`, `T-Slice-From-Slice@L36749`, `T-Slice-From-Slice-Perm@L36767`, `P-Index-Slice@L36785`, `P-Index-Slice-Perm@L36803`, `P-Slice-From-Array@L36821`
- `P-Slice-From-Array-Perm@L36839`, `P-Slice-From-Slice@L36857`, `P-Slice-From-Slice-Perm@L36875`, `Coerce-Array-Slice@L36893`, `Index-NonIndexable@L36911`, `EvalSigma-Index-Range@L36960`, `EvalSigma-Index-Range-OOB@L36978`, `Size-Slice@L36998`
- `Align-Slice@L37015`, `Layout-Slice@L37032`, `Index-Slice-NonUsize@L37079`, `req.SliceDiagnosticOwnership@L37097`

#### `checker.types.ranges`

Count: 55 total; 55 required; 0 recommended; 0 informative. Ledger line span: L36813-L37833.

- `Parse-Range-To@L37154`, `Parse-Range-ToInc@L37172`, `Parse-Range-Full@L37190`, `Parse-Range-Lhs@L37208`, `Parse-RangeTail-None@L37226`, `Parse-RangeTail-From@L37244`, `Parse-RangeTail-Exclusive@L37262`, `Parse-RangeTail-Inclusive@L37280`
- `def.RangeSurfaceTypeElaboration@L37298`, `def.RangeTypeAst@L37319`, `def.RangeExprAst@L37335`, `def.IsRangeType@L37349`, `def.RangeFullExprType@L37363`, `def.RangeToExprType@L37379`, `def.RangeToInclusiveExprType@L37393`, `def.RangeFromExprType@L37407`
- `def.RangeExclusiveExprType@L37421`, `def.RangeInclusiveExprType@L37435`, `T-Range-Lift@L37449`, `Range-Full@L37467`, `Range-To@L37484`, `Range-ToInclusive@L37502`, `Range-From@L37520`, `Range-Exclusive@L37538`
- `Range-Inclusive@L37556`, `req.RangePatternSemanticsOwnership@L37574`, `EvalSigma-Range@L37609`, `EvalSigma-Range-Ctrl@L37627`, `EvalSigma-Range-Ctrl-Hi@L37645`, `def.RangeInc@L37663`, `Lower-Range-Full@L37746`, `Lower-Range-To@L37763`
- `Lower-Range-ToInclusive@L37781`, `Lower-Range-From@L37799`, `Lower-Range-Inclusive@L37817`, `Lower-Range-Exclusive@L37835`, `Size-Range@L37853`, `Align-Range@L37871`, `Layout-Range@L37889`, `Size-RangeInclusive@L37907`
- `Align-RangeInclusive@L37925`, `Layout-RangeInclusive@L37943`, `Size-RangeFrom@L37961`, `Align-RangeFrom@L37979`, `Layout-RangeFrom@L37997`, `Size-RangeTo@L38015`, `Align-RangeTo@L38033`, `Layout-RangeTo@L38051`
- `Size-RangeToInclusive@L38069`, `Align-RangeToInclusive@L38087`, `Layout-RangeToInclusive@L38105`, `Size-RangeFull@L38123`, `Align-RangeFull@L38140`, `Layout-RangeFull@L38157`, `req.RangeDiagnosticOwnership@L38174`
- `Parse-Range-To@L37154`, `Parse-Range-ToInc@L37172`, `Parse-Range-Full@L37190`, `Parse-Range-Lhs@L37208`, `Parse-RangeTail-None@L37226`, `Parse-RangeTail-From@L37244`, `Parse-RangeTail-Exclusive@L37262`, `Parse-RangeTail-Inclusive@L37280`
- `def.RangeSurfaceTypeElaboration@L37298`, `def.RangeTypeAst@L37319`, `def.RangeExprAst@L37335`, `def.IsRangeType@L37349`, `def.RangeFullExprType@L37363`, `def.RangeToExprType@L37379`, `def.RangeToInclusiveExprType@L37393`, `def.RangeFromExprType@L37407`
- `def.RangeExclusiveExprType@L37421`, `def.RangeInclusiveExprType@L37435`, `T-Range-Lift@L37449`, `Range-Full@L37467`, `Range-To@L37484`, `Range-ToInclusive@L37502`, `Range-From@L37520`, `Range-Exclusive@L37538`
- `Range-Inclusive@L37556`, `req.RangePatternSemanticsOwnership@L37574`, `EvalSigma-Range@L37609`, `EvalSigma-Range-Ctrl@L37627`, `EvalSigma-Range-Ctrl-Hi@L37645`, `def.RangeInc@L37663`, `Lower-Range-Full@L37746`, `Lower-Range-To@L37763`
- `Lower-Range-ToInclusive@L37781`, `Lower-Range-From@L37799`, `Lower-Range-Inclusive@L37817`, `Lower-Range-Exclusive@L37835`, `Size-Range@L37853`, `Align-Range@L37871`, `Layout-Range@L37889`, `Size-RangeInclusive@L37907`
- `Align-RangeInclusive@L37925`, `Layout-RangeInclusive@L37943`, `Size-RangeFrom@L37961`, `Align-RangeFrom@L37979`, `Layout-RangeFrom@L37997`, `Size-RangeTo@L38015`, `Align-RangeTo@L38033`, `Layout-RangeTo@L38051`
- `Size-RangeToInclusive@L38069`, `Align-RangeToInclusive@L38087`, `Layout-RangeToInclusive@L38105`, `Size-RangeFull@L38123`, `Align-RangeFull@L38140`, `Layout-RangeFull@L38157`, `req.RangeDiagnosticOwnership@L38174`

#### `spec.modal-special`

Count: 386 total; 386 required; 0 recommended; 0 informative. Ledger line span: L40979-L46752.

- `grammar.ModalDeclarations.Syntax@L41320`, `rule.13.Parse-Modal@L41342`, `rule.13.Parse-ModalBody@L41358`, `rule.13.Parse-StateBlock@L41374`, `def.ModalTypeRef.Parser@L41392`, `rule.13.Parse-Modal-State-Type@L41404`, `rule.13.Parse-Record-Literal-ModalState@L41420`, `req.ModalStateMemberDispatch@L41436`
- `def.ModalDeclAst@L41450`, `def.StateBlockAst@L41463`, `def.StateMemberAst@L41475`, `def.ModalRefAst@L41491`, `def.TypeRefModalStateAst@L41503`, `def.ModalStateAccessors@L41515`, `def.ModalStatePayload@L41532`, `def.BuiltinModalSet@L41544`
- `def.ModalPath@L41556`, `def.ModalSelfRef@L41569`, `def.ModalSelfTypes@L41584`, `def.ModalRefAccessors@L41597`, `def.ModalDeclOf@L41614`, `def.ModalRefSubst@L41626`, `def.ModalPayloadSubstitution@L41638`, `def.PayloadMap@L41650`
- `def.ModalPayloadMap@L41664`, `rule.13.WF-Modal-Payload@L41680`, `rule.13.Modal-Payload-DupField@L41696`, `rule.13.WF-ModalState@L41712`, `rule.13.WF-ModalState-ArgCount-Err@L41728`, `def.StateMemberVisOk@L41744`, `rule.13.WF-ModalDecl@L41756`, `rule.13.StateMemberVisOk-Err@L41772`
- `rule.13.Modal-WF@L41788`, `rule.13.Modal-NoStates-Err@L41804`, `rule.13.Modal-DupState-Err@L41820`, `rule.13.Modal-StateName-Err@L41836`, `rule.13.State-Specific-WF@L41852`, `def.ModalPayloadNames@L41868`, `rule.13.T-Modal-State-Intro@L41881`, `rule.13.Record-FileDir-Err@L41897`
- `def.RegionPayload@L41915`, `def.RegionProcSet@L41930`, `def.RegionNewScopedProcSig@L41942`, `def.RegionAllocProcSig@L41954`, `def.RegionResetUncheckedProcSig@L41966`, `def.RegionFreezeProcSig@L41978`, `def.RegionThawProcSig@L41990`, `def.RegionFreeUncheckedProcSig@L42002`
- `def.RegionProvenanceTypeHelpers@L42014`, `def.RegionNonBitcopy@L42028`, `req.RegionAllocProvenance@L42042`, `req.RegionInactiveDereferenceSemantics@L42054`, `req.RegionFreeAtScopeExit@L42066`, `rule.13.Region-Unchecked-Unsafe-Err@L42078`, `def.CancelTokenTypePresence@L42094`, `def.CancelTokenPayload@L42107`
- `def.CancelTokenMembersAndDecl@L42120`, `def.CancelTokenTypeBinding@L42142`, `def.CancelTokenProcSignatures@L42154`, `def.SpawnedTypePresence@L42167`, `def.SpawnedPayload@L42180`, `def.SpawnedMembersAndDecl@L42196`, `def.SpawnedTypeBinding@L42216`, `def.TrackedTypePresence@L42228`
- `def.TrackedPayload@L42241`, `def.TrackedMembersAndDecl@L42257`, `def.TrackedTypeBinding@L42277`, `def.DirIterMembersAndDecl@L42289`, `def.FileMembersAndDecl@L42311`, `def.DirIterAndFileTypeBindings@L42348`, `req.AsyncModalDefinedInChapter21@L42361`, `def.ModalValRuntime@L42375`
- `def.RecordModalStateValueType@L42387`, `def.ModalValValueType@L42399`, `req.ModalRuntimeRepresentation@L42411`, `def.ModalDiscType@L42425`, `def.ModalStateLayoutMetrics@L42437`, `def.ModalSingleFieldPayload@L42450`, `def.ModalEmptyState@L42463`, `def.ModalPayloadState@L42475`
- `def.ModalNicheApplies@L42487`, `def.ModalStateValueBits@L42499`, `def.ModalEmptyStates@L42511`, `def.EmptyRecordVal@L42523`, `def.ModalNicheBits@L42535`, `def.ModalBits@L42547`, `def.ModalAlign@L42559`, `def.ModalSize@L42571`
- `def.ModalPayloadSize@L42583`, `def.ModalPayloadAlign@L42595`, `def.StateRecordBits@L42607`, `def.ModalPayloadBits@L42619`, `def.ModalLayoutJudgementSet@L42631`, `rule.13.Layout-Modal-Niche@L42643`, `rule.13.Layout-Modal-Tagged@L42659`, `rule.13.Size-Modal@L42675`
- `rule.13.Align-Modal@L42691`, `rule.13.Layout-Modal@L42707`, `rule.13.Size-ModalState@L42723`, `rule.13.Align-ModalState@L42739`, `rule.13.Layout-ModalState@L42755`, `def.ModalStateLayoutEquation@L42771`, `def.EmptyModalStateSizeEquation@L42783`, `def.ModalBaseLayoutEquation@L42795`
- `req.ModalTaggedPaddingZero@L42809`, `def.ModalTaggedBits@L42820`, `def.ModalRefValueBits@L42832`, `diag.ModalDeclarations@L42846`, `grammar.StateFields.Syntax@L42862`, `rule.13.Parse-StateMember-Field@L42878`, `def.StateFieldDeclAst@L42896`, `def.PayloadNameHelpers@L42908`
- `def.ModalFieldVisible@L42923`, `rule.13.T-Modal-Field@L42935`, `rule.13.T-Modal-Field-Perm@L42951`, `rule.13.Modal-Field-Missing@L42967`, `rule.13.Modal-Field-General-Err@L42983`, `rule.13.Modal-Field-NotVisible@L42999`, `req.StateFieldDynamic@L43017`, `req.StateFieldLowering@L43031`
- `diag.StateFields@L43045`, `grammar.StateMethods.Syntax@L43061`, `rule.13.Parse-StateMember-Method@L43078`, `req.StateMethodSignatureParser@L43094`, `def.StateMethodDeclAst@L43108`, `def.StateMethodCollections@L43120`, `def.StateMethodSig@L43133`, `def.LookupStateMethod@L43147`
- `def.StateMemberVisible@L43162`, `rule.13.StateMethod-Dup@L43174`, `rule.13.WF-State-Method@L43190`, `rule.13.T-Modal-Method@L43206`, `rule.13.Modal-Method-RecvPerm-Err@L43222`, `rule.13.Modal-Method-NotFound@L43238`, `rule.13.Modal-Method-NotVisible@L43254`, `rule.13.T-Modal-Method-Body@L43270`
- `def.StateMethodTarget@L43288`, `rule.13.ApplyMethodSigma@L43300`, `req.BuiltinStateMethodCalling@L43316`, `req.StateMethodLowering@L43330`, `diag.StateMethods@L43344`, `grammar.Transitions.Syntax@L43360`, `rule.13.Parse-StateMember-Transition@L43376`, `def.TransitionDeclAst@L43394`
- `def.TransitionCollections@L43406`, `def.LookupTransition@L43420`, `def.TransitionSig@L43433`, `rule.13.Transition-Dup@L43452`, `rule.13.StateMember-Name-Conflict@L43468`, `rule.13.WF-Transition@L43484`, `rule.13.Transition-Target-Err@L43500`, `rule.13.T-Modal-Transition@L43516`
- `rule.13.Transition-Source-Err@L43532`, `rule.13.Transition-NotVisible@L43548`, `rule.13.T-Modal-Transition-Body@L43564`, `rule.13.Transition-Body-Err@L43580`, `def.TransitionMethodTarget@L43598`, `req.TransitionRuntimeSemantics@L43610`, `def.IsTransition@L43622`, `def.TransitionTarget@L43634`
- `rule.13.ApplyTransitionSigma@L43646`, `def.ExtractReturnValue@L43664`, `def.ValidateModalState@L43677`, `req.TransitionLowering@L43691`, `diag.Transitions@L43705`, `grammar.ModalWidening.Syntax@L43721`, `rule.13.Parse-Unary-Widen@L43737`, `def.ModalWidening.AST@L43755`
- `def.ModalWideningThreshold@L43769`, `rule.13.T-Modal-Widen@L43781`, `rule.13.T-Modal-Widen-Perm@L43797`, `rule.13.Widen-AlreadyGeneral@L43813`, `rule.13.Widen-NonModal@L43829`, `def.NicheCompatible@L43845`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43857`, `def.WidenWarnCond@L43873`
- `rule.13.Warn-Widen-LargePayload@L43885`, `rule.13.Warn-Widen-Ok@L43901`, `def.ModalWideningDynamic@L43919`, `req.ModalWideningLowering@L43933`, `def.ModalStateSizeBound@L43945`, `diag.ModalWidening@L43959`, `grammar.StringTypes.Syntax@L43975`, `rule.13.Parse-String-Type@L43992`
- `rule.13.Parse-StringState-None@L44010`, `rule.13.Parse-StringState-Managed@L44026`, `rule.13.Parse-StringState-View@L44042`, `def.TypeStringAst@L44060`, `def.StringStateSet@L44072`, `def.StringBuiltinTable@L44084`, `def.StringBuiltinSig@L44106`, `rule.13.WF-String@L44120`
- `rule.13.Sub-String-State@L44136`, `req.StringBuiltinsTyping@L44150`, `def.StringLiteralVal@L44164`, `def.StringBytesStoreDomains@L44176`, `def.ViewBytes@L44192`, `def.ByteSeqOf@L44204`, `def.ByteLen@L44219`, `def.StringValueTypes@L44231`
- `def.StringBytesJudgementSet@L44964`, `req.StringLiteralStorage@L44266`, `rule.13.StringFrom-Ok@L44280`, `rule.13.StringFrom-Err@L44296`, `rule.13.StringAsView-Ok@L44312`, `rule.13.StringToManaged-Ok@L44344`, `rule.13.StringToManaged-Err@L44360`, `rule.13.StringCloneWith-Ok@L44376`
- `rule.13.StringCloneWith-Err@L44392`, `rule.13.StringAppend-Ok@L44408`, `rule.13.StringAppend-Err@L44424`, `rule.13.StringLength@L44440`, `rule.13.StringIsEmpty@L44456`, `def.StringViewOf@L44472`, `def.StringRuntimeLength@L44486`, `def.StringManagedLoweringLayout@L44501`
- `def.StringViewLoweringLayout@L44515`, `rule.13.Size-String-Managed@L44529`, `rule.13.Align-String-Managed@L44545`, `rule.13.Layout-String-Managed@L44561`, `rule.13.Size-String-View@L44577`, `rule.13.Align-String-View@L44593`, `rule.13.Layout-String-View@L44609`, `rule.13.Size-String-Modal@L44625`
- `rule.13.Align-String-Modal@L44641`, `def.StringValueBits@L44657`, `def.DropManagedString@L44669`, `diag.StringTypes@L44683`, `grammar.BytesTypes.Syntax@L44699`, `rule.13.Parse-Bytes-Type@L44716`, `rule.13.Parse-BytesState-None@L44734`, `rule.13.Parse-BytesState-Managed@L44750`
- `rule.13.Parse-BytesState-View@L44766`, `def.TypeBytesAst@L44784`, `def.BytesStateSet@L44796`, `def.BytesBuiltinTable@L44808`, `def.StringBytesBuiltinTable@L44832`, `def.BytesBuiltinSig@L44844`, `def.StringBytesBuiltinSig@L44856`, `rule.13.WF-Bytes@L44871`
- `rule.13.Sub-Bytes-State@L44887`, `req.BytesBuiltinsTyping@L44901`, `def.SliceBytes@L44915`, `def.BytesValueTypes@L44927`, `def.BytesJudgementSet@L44941`, `def.StringBytesJudgementSet@L44964`, `rule.13.BytesWithCapacity-Ok@L44976`, `rule.13.BytesWithCapacity-Err@L44992`
- `rule.13.BytesFromSlice-Ok@L45008`, `rule.13.BytesFromSlice-Err@L45024`, `rule.13.BytesAsView-Ok@L45040`, `rule.13.BytesToManaged-Ok@L45056`, `rule.13.BytesToManaged-Err@L45072`, `rule.13.BytesView-Ok@L45088`, `rule.13.BytesViewString-Ok@L45104`, `rule.13.BytesAsSlice-Ok@L45120`
- `rule.13.BytesAppend-Ok@L45136`, `rule.13.BytesAppend-Err@L45152`, `rule.13.BytesLength@L45168`, `rule.13.BytesIsEmpty@L45184`, `def.BytesViewOf@L45200`, `def.BytesRuntimeLength@L45214`, `def.BytesViewConversions@L45227`, `def.BytesManagedLoweringLayout@L45242`
- `def.BytesViewLoweringLayout@L45256`, `rule.13.Size-Bytes-Managed@L45270`, `rule.13.Align-Bytes-Managed@L45286`, `rule.13.Layout-Bytes-Managed@L45302`, `rule.13.Size-Bytes-View@L45318`, `rule.13.Align-Bytes-View@L45334`, `rule.13.Layout-Bytes-View@L45350`, `rule.13.Size-Bytes-Modal@L45366`
- `rule.13.Align-Bytes-Modal@L45382`, `def.BytesValueBits@L45398`, `def.DropManagedBytes@L45410`, `diag.BytesTypes@L45424`, `grammar.SafePointerTypes.Syntax@L45440`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45457`, `rule.13.Parse-Safe-Pointer-Type@L45473`, `rule.13.Parse-PtrState-None@L45491`
- `rule.13.Parse-PtrState-Valid@L45507`, `rule.13.Parse-PtrState-Null@L45523`, `rule.13.Parse-PtrState-Expired@L45539`, `def.PtrStateSet@L45557`, `def.SafePointerTypeForms@L45569`, `rule.13.WF-Ptr@L45584`, `def.SafePointerTraits@L45600`, `rule.13.Sub-Ptr-State@L45614`
- `def.SafePointerRuntimeConstructors@L45632`, `def.SafePointerValueType@L45646`, `def.PtrStateImmediate@L45658`, `def.PtrStateValid@L45671`, `def.PtrAddrJudgementSet@L45686`, `rule.13.ReadPtr-Safe@L45698`, `rule.13.WritePtr-Safe@L45714`, `rule.13.ReadPtr-Null@L45730`
- `rule.13.ReadPtr-Expired@L45746`, `rule.13.WritePtr-Null@L45762`, `rule.13.WritePtr-Expired@L45778`, `rule.13.Size-Ptr@L45796`, `rule.13.Align-Ptr@L45812`, `rule.13.Layout-Ptr@L45828`, `def.SafePointerSizeAlignEquations@L45844`, `def.PtrDiagRefs@L45856`
- `def.SafePointerNicheSet@L45868`, `def.SafePointerValidValue@L45881`, `def.SafePointerValueBits@L45896`, `diag.SafePointerTypes@L45913`, `grammar.RawPointerTypes.Syntax@L45929`, `rule.13.Parse-Raw-Pointer-Type@L45945`, `def.RawPointerTypes.AST@L45963`, `rule.13.WF-RawPtr@L45977`
- `rule.13.T-Deref-Raw@L45993`, `rule.13.P-Deref-Raw-Imm@L46009`, `rule.13.P-Deref-Raw-Mut@L46025`, `rule.13.Deref-Raw-Unsafe@L46041`, `def.RawPointerRuntimeValue@L46059`, `rule.13.ReadPtr-Raw@L46071`, `rule.13.WritePtr-Raw@L46087`, `rule.13.ReadPtr-Raw-Invalid@L46103`
- `rule.13.WritePtr-Raw-Imm@L46119`, `rule.13.WritePtr-Raw-Invalid@L46135`, `rule.13.Size-RawPtr@L46153`, `rule.13.Align-RawPtr@L46169`, `rule.13.Layout-RawPtr@L46185`, `def.RawPointerValidValue@L46201`, `def.RawPointerValueBits@L46213`, `req.RawPointerLowering@L46224`
- `diag.RawPointerTypes@L46238`, `grammar.FunctionTypes.Syntax@L46254`, `req.FunctionTypeTrailingComma@L46270`, `rule.13.Parse-Func-Type@L46284`, `rule.13.Parse-ParamType-Move@L46300`, `rule.13.Parse-ParamType-Plain@L46316`, `rule.13.Parse-ParamTypeList-Empty@L46332`, `rule.13.Parse-ParamTypeList-Cons@L46348`
- `rule.13.Parse-ParamTypeListTail-End@L46364`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46380`, `rule.13.Parse-ParamTypeListTail-Cons@L46396`, `def.FunctionTypes.AST@L46414`, `rule.13.WF-Func@L46428`, `rule.13.T-Equiv-Func@L46444`, `rule.13.Sub-Func@L46460`, `rule.13.T-Proc-As-Value@L46476`
- `diag.FunctionTypeCalls@L46492`, `def.FunctionRuntimeValue@L46506`, `rule.13.EvalSigma-Call-Proc@L46518`, `req.NamedProceduresFirstClass@L46534`, `rule.13.Size-Func@L46548`, `rule.13.Align-Func@L46564`, `rule.13.Layout-Func@L46580`, `req.FunctionTypeCallLowering@L46596`
- `diag.FunctionTypes@L46610`, `grammar.ClosureTypes.Syntax@L46626`, `req.ClosureParamUnionParentheses@L46643`, `rule.13.Parse-Closure-Type@L46657`, `rule.13.Parse-Closure-Type-Empty@L46673`, `rule.13.Parse-ClosureParamType-Grouped@L46689`, `rule.13.Parse-ClosureParamType-Plain@L46705`, `rule.13.Parse-ClosureParamTypeList-Empty@L46721`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46737`, `rule.13.Parse-ClosureParamTypeListTail-End@L46753`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46769`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46785`, `rule.13.Parse-ClosureDepsOpt-None@L46801`, `rule.13.Parse-ClosureDepsOpt-Some@L46817`, `rule.13.Parse-SharedDepList-Empty@L46833`, `rule.13.Parse-SharedDepList-Single@L46849`
- `rule.13.Parse-SharedDepList-Cons@L46865`, `rule.13.Parse-SharedDep@L46881`, `def.TypeClosureAst@L46899`, `def.ClosureDepsOpt@L46911`, `req.ClosureTypeOwnershipBoundaries@L46923`, `rule.13.WF-Closure@L46937`, `rule.13.T-Equiv-Closure@L46953`, `rule.13.Sub-Closure@L46969`
- `req.ClosureExpressionOwnership@L46985`, `def.ClosureRuntimeValue@L46999`, `req.ClosureOperationOwnership@L47011`, `def.ClosureLoweringRep@L47025`, `rule.13.Size-Closure@L47037`, `rule.13.Align-Closure@L47053`, `rule.13.Layout-Closure@L47069`, `req.ClosureLoweringOwnership@L47085`
- `diag.ClosureTypes@L47099`, `diagnostics.ModalPointerSupplement@L47111`
- `grammar.ModalDeclarations.Syntax@L41320`, `rule.13.Parse-Modal@L41342`, `rule.13.Parse-ModalBody@L41358`, `rule.13.Parse-StateBlock@L41374`, `def.ModalTypeRef.Parser@L41392`, `rule.13.Parse-Modal-State-Type@L41404`, `rule.13.Parse-Record-Literal-ModalState@L41420`, `req.ModalStateMemberDispatch@L41436`
- `def.ModalDeclAst@L41450`, `def.StateBlockAst@L41463`, `def.StateMemberAst@L41475`, `def.ModalRefAst@L41491`, `def.TypeRefModalStateAst@L41503`, `def.ModalStateAccessors@L41515`, `def.ModalStatePayload@L41532`, `def.BuiltinModalSet@L41544`
- `def.ModalPath@L41556`, `def.ModalSelfRef@L41569`, `def.ModalSelfTypes@L41584`, `def.ModalRefAccessors@L41597`, `def.ModalDeclOf@L41614`, `def.ModalRefSubst@L41626`, `def.ModalPayloadSubstitution@L41638`, `def.PayloadMap@L41650`
- `def.ModalPayloadMap@L41664`, `rule.13.WF-Modal-Payload@L41680`, `rule.13.Modal-Payload-DupField@L41696`, `rule.13.WF-ModalState@L41712`, `rule.13.WF-ModalState-ArgCount-Err@L41728`, `def.StateMemberVisOk@L41744`, `rule.13.WF-ModalDecl@L41756`, `rule.13.StateMemberVisOk-Err@L41772`
- `rule.13.Modal-WF@L41788`, `rule.13.Modal-NoStates-Err@L41804`, `rule.13.Modal-DupState-Err@L41820`, `rule.13.Modal-StateName-Err@L41836`, `rule.13.State-Specific-WF@L41852`, `def.ModalPayloadNames@L41868`, `rule.13.T-Modal-State-Intro@L41881`, `rule.13.Record-FileDir-Err@L41897`
- `def.RegionPayload@L41915`, `def.RegionProcSet@L41930`, `def.RegionNewScopedProcSig@L41942`, `def.RegionAllocProcSig@L41954`, `def.RegionResetUncheckedProcSig@L41966`, `def.RegionFreezeProcSig@L41978`, `def.RegionThawProcSig@L41990`, `def.RegionFreeUncheckedProcSig@L42002`
- `def.RegionProvenanceTypeHelpers@L42014`, `def.RegionNonBitcopy@L42028`, `req.RegionAllocProvenance@L42042`, `req.RegionInactiveDereferenceSemantics@L42054`, `req.RegionFreeAtScopeExit@L42066`, `rule.13.Region-Unchecked-Unsafe-Err@L42078`, `def.CancelTokenTypePresence@L42094`, `def.CancelTokenPayload@L42107`
- `def.CancelTokenMembersAndDecl@L42120`, `def.CancelTokenTypeBinding@L42142`, `def.CancelTokenProcSignatures@L42154`, `def.SpawnedTypePresence@L42167`, `def.SpawnedPayload@L42180`, `def.SpawnedMembersAndDecl@L42196`, `def.SpawnedTypeBinding@L42216`, `def.TrackedTypePresence@L42228`
- `def.TrackedPayload@L42241`, `def.TrackedMembersAndDecl@L42257`, `def.TrackedTypeBinding@L42277`, `def.DirIterMembersAndDecl@L42289`, `def.FileMembersAndDecl@L42311`, `def.DirIterAndFileTypeBindings@L42348`, `req.AsyncModalDefinedInChapter21@L42361`, `def.ModalValRuntime@L42375`
- `def.RecordModalStateValueType@L42387`, `def.ModalValValueType@L42399`, `req.ModalRuntimeRepresentation@L42411`, `def.ModalDiscType@L42425`, `def.ModalStateLayoutMetrics@L42437`, `def.ModalSingleFieldPayload@L42450`, `def.ModalEmptyState@L42463`, `def.ModalPayloadState@L42475`
- `def.ModalNicheApplies@L42487`, `def.ModalStateValueBits@L42499`, `def.ModalEmptyStates@L42511`, `def.EmptyRecordVal@L42523`, `def.ModalNicheBits@L42535`, `def.ModalBits@L42547`, `def.ModalAlign@L42559`, `def.ModalSize@L42571`
- `def.ModalPayloadSize@L42583`, `def.ModalPayloadAlign@L42595`, `def.StateRecordBits@L42607`, `def.ModalPayloadBits@L42619`, `def.ModalLayoutJudgementSet@L42631`, `rule.13.Layout-Modal-Niche@L42643`, `rule.13.Layout-Modal-Tagged@L42659`, `rule.13.Size-Modal@L42675`
- `rule.13.Align-Modal@L42691`, `rule.13.Layout-Modal@L42707`, `rule.13.Size-ModalState@L42723`, `rule.13.Align-ModalState@L42739`, `rule.13.Layout-ModalState@L42755`, `def.ModalStateLayoutEquation@L42771`, `def.EmptyModalStateSizeEquation@L42783`, `def.ModalBaseLayoutEquation@L42795`
- `req.ModalTaggedPaddingZero@L42809`, `def.ModalTaggedBits@L42820`, `def.ModalRefValueBits@L42832`, `diag.ModalDeclarations@L42846`, `grammar.StateFields.Syntax@L42862`, `rule.13.Parse-StateMember-Field@L42878`, `def.StateFieldDeclAst@L42896`, `def.PayloadNameHelpers@L42908`
- `def.ModalFieldVisible@L42923`, `rule.13.T-Modal-Field@L42935`, `rule.13.T-Modal-Field-Perm@L42951`, `rule.13.Modal-Field-Missing@L42967`, `rule.13.Modal-Field-General-Err@L42983`, `rule.13.Modal-Field-NotVisible@L42999`, `req.StateFieldDynamic@L43017`, `req.StateFieldLowering@L43031`
- `diag.StateFields@L43045`, `grammar.StateMethods.Syntax@L43061`, `rule.13.Parse-StateMember-Method@L43078`, `req.StateMethodSignatureParser@L43094`, `def.StateMethodDeclAst@L43108`, `def.StateMethodCollections@L43120`, `def.StateMethodSig@L43133`, `def.LookupStateMethod@L43147`
- `def.StateMemberVisible@L43162`, `rule.13.StateMethod-Dup@L43174`, `rule.13.WF-State-Method@L43190`, `rule.13.T-Modal-Method@L43206`, `rule.13.Modal-Method-RecvPerm-Err@L43222`, `rule.13.Modal-Method-NotFound@L43238`, `rule.13.Modal-Method-NotVisible@L43254`, `rule.13.T-Modal-Method-Body@L43270`
- `def.StateMethodTarget@L43288`, `rule.13.ApplyMethodSigma@L43300`, `req.BuiltinStateMethodCalling@L43316`, `req.StateMethodLowering@L43330`, `diag.StateMethods@L43344`, `grammar.Transitions.Syntax@L43360`, `rule.13.Parse-StateMember-Transition@L43376`, `def.TransitionDeclAst@L43394`
- `def.TransitionCollections@L43406`, `def.LookupTransition@L43420`, `def.TransitionSig@L43433`, `rule.13.Transition-Dup@L43452`, `rule.13.StateMember-Name-Conflict@L43468`, `rule.13.WF-Transition@L43484`, `rule.13.Transition-Target-Err@L43500`, `rule.13.T-Modal-Transition@L43516`
- `rule.13.Transition-Source-Err@L43532`, `rule.13.Transition-NotVisible@L43548`, `rule.13.T-Modal-Transition-Body@L43564`, `rule.13.Transition-Body-Err@L43580`, `def.TransitionMethodTarget@L43598`, `req.TransitionRuntimeSemantics@L43610`, `def.IsTransition@L43622`, `def.TransitionTarget@L43634`
- `rule.13.ApplyTransitionSigma@L43646`, `def.ExtractReturnValue@L43664`, `def.ValidateModalState@L43677`, `req.TransitionLowering@L43691`, `diag.Transitions@L43705`, `grammar.ModalWidening.Syntax@L43721`, `rule.13.Parse-Unary-Widen@L43737`, `def.ModalWidening.AST@L43755`
- `def.ModalWideningThreshold@L43769`, `rule.13.T-Modal-Widen@L43781`, `rule.13.T-Modal-Widen-Perm@L43797`, `rule.13.Widen-AlreadyGeneral@L43813`, `rule.13.Widen-NonModal@L43829`, `def.NicheCompatible@L43845`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43857`, `def.WidenWarnCond@L43873`
- `rule.13.Warn-Widen-LargePayload@L43885`, `rule.13.Warn-Widen-Ok@L43901`, `def.ModalWideningDynamic@L43919`, `req.ModalWideningLowering@L43933`, `def.ModalStateSizeBound@L43945`, `diag.ModalWidening@L43959`, `grammar.StringTypes.Syntax@L43975`, `rule.13.Parse-String-Type@L43992`
- `rule.13.Parse-StringState-None@L44010`, `rule.13.Parse-StringState-Managed@L44026`, `rule.13.Parse-StringState-View@L44042`, `def.TypeStringAst@L44060`, `def.StringStateSet@L44072`, `def.StringBuiltinTable@L44084`, `def.StringBuiltinSig@L44106`, `rule.13.WF-String@L44120`
- `rule.13.Sub-String-State@L44136`, `req.StringBuiltinsTyping@L44150`, `def.StringLiteralVal@L44164`, `def.StringBytesStoreDomains@L44176`, `def.ViewBytes@L44192`, `def.ByteSeqOf@L44204`, `def.ByteLen@L44219`, `def.StringValueTypes@L44231`
- `def.StringBytesJudgementSet@L44964`, `req.StringLiteralStorage@L44266`, `rule.13.StringFrom-Ok@L44280`, `rule.13.StringFrom-Err@L44296`, `rule.13.StringAsView-Ok@L44312`, `rule.13.StringToManaged-Ok@L44344`, `rule.13.StringToManaged-Err@L44360`, `rule.13.StringCloneWith-Ok@L44376`
- `rule.13.StringCloneWith-Err@L44392`, `rule.13.StringAppend-Ok@L44408`, `rule.13.StringAppend-Err@L44424`, `rule.13.StringLength@L44440`, `rule.13.StringIsEmpty@L44456`, `def.StringViewOf@L44472`, `def.StringRuntimeLength@L44486`, `def.StringManagedLoweringLayout@L44501`
- `def.StringViewLoweringLayout@L44515`, `rule.13.Size-String-Managed@L44529`, `rule.13.Align-String-Managed@L44545`, `rule.13.Layout-String-Managed@L44561`, `rule.13.Size-String-View@L44577`, `rule.13.Align-String-View@L44593`, `rule.13.Layout-String-View@L44609`, `rule.13.Size-String-Modal@L44625`
- `rule.13.Align-String-Modal@L44641`, `def.StringValueBits@L44657`, `def.DropManagedString@L44669`, `diag.StringTypes@L44683`, `grammar.BytesTypes.Syntax@L44699`, `rule.13.Parse-Bytes-Type@L44716`, `rule.13.Parse-BytesState-None@L44734`, `rule.13.Parse-BytesState-Managed@L44750`
- `rule.13.Parse-BytesState-View@L44766`, `def.TypeBytesAst@L44784`, `def.BytesStateSet@L44796`, `def.BytesBuiltinTable@L44808`, `def.StringBytesBuiltinTable@L44832`, `def.BytesBuiltinSig@L44844`, `def.StringBytesBuiltinSig@L44856`, `rule.13.WF-Bytes@L44871`
- `rule.13.Sub-Bytes-State@L44887`, `req.BytesBuiltinsTyping@L44901`, `def.SliceBytes@L44915`, `def.BytesValueTypes@L44927`, `def.BytesJudgementSet@L44941`, `def.StringBytesJudgementSet@L44964`, `rule.13.BytesWithCapacity-Ok@L44976`, `rule.13.BytesWithCapacity-Err@L44992`
- `rule.13.BytesFromSlice-Ok@L45008`, `rule.13.BytesFromSlice-Err@L45024`, `rule.13.BytesAsView-Ok@L45040`, `rule.13.BytesToManaged-Ok@L45056`, `rule.13.BytesToManaged-Err@L45072`, `rule.13.BytesView-Ok@L45088`, `rule.13.BytesViewString-Ok@L45104`, `rule.13.BytesAsSlice-Ok@L45120`
- `rule.13.BytesAppend-Ok@L45136`, `rule.13.BytesAppend-Err@L45152`, `rule.13.BytesLength@L45168`, `rule.13.BytesIsEmpty@L45184`, `def.BytesViewOf@L45200`, `def.BytesRuntimeLength@L45214`, `def.BytesViewConversions@L45227`, `def.BytesManagedLoweringLayout@L45242`
- `def.BytesViewLoweringLayout@L45256`, `rule.13.Size-Bytes-Managed@L45270`, `rule.13.Align-Bytes-Managed@L45286`, `rule.13.Layout-Bytes-Managed@L45302`, `rule.13.Size-Bytes-View@L45318`, `rule.13.Align-Bytes-View@L45334`, `rule.13.Layout-Bytes-View@L45350`, `rule.13.Size-Bytes-Modal@L45366`
- `rule.13.Align-Bytes-Modal@L45382`, `def.BytesValueBits@L45398`, `def.DropManagedBytes@L45410`, `diag.BytesTypes@L45424`, `grammar.SafePointerTypes.Syntax@L45440`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45457`, `rule.13.Parse-Safe-Pointer-Type@L45473`, `rule.13.Parse-PtrState-None@L45491`
- `rule.13.Parse-PtrState-Valid@L45507`, `rule.13.Parse-PtrState-Null@L45523`, `rule.13.Parse-PtrState-Expired@L45539`, `def.PtrStateSet@L45557`, `def.SafePointerTypeForms@L45569`, `rule.13.WF-Ptr@L45584`, `def.SafePointerTraits@L45600`, `rule.13.Sub-Ptr-State@L45614`
- `def.SafePointerRuntimeConstructors@L45632`, `def.SafePointerValueType@L45646`, `def.PtrStateImmediate@L45658`, `def.PtrStateValid@L45671`, `def.PtrAddrJudgementSet@L45686`, `rule.13.ReadPtr-Safe@L45698`, `rule.13.WritePtr-Safe@L45714`, `rule.13.ReadPtr-Null@L45730`
- `rule.13.ReadPtr-Expired@L45746`, `rule.13.WritePtr-Null@L45762`, `rule.13.WritePtr-Expired@L45778`, `rule.13.Size-Ptr@L45796`, `rule.13.Align-Ptr@L45812`, `rule.13.Layout-Ptr@L45828`, `def.SafePointerSizeAlignEquations@L45844`, `def.PtrDiagRefs@L45856`
- `def.SafePointerNicheSet@L45868`, `def.SafePointerValidValue@L45881`, `def.SafePointerValueBits@L45896`, `diag.SafePointerTypes@L45913`, `grammar.RawPointerTypes.Syntax@L45929`, `rule.13.Parse-Raw-Pointer-Type@L45945`, `def.RawPointerTypes.AST@L45963`, `rule.13.WF-RawPtr@L45977`
- `rule.13.T-Deref-Raw@L45993`, `rule.13.P-Deref-Raw-Imm@L46009`, `rule.13.P-Deref-Raw-Mut@L46025`, `rule.13.Deref-Raw-Unsafe@L46041`, `def.RawPointerRuntimeValue@L46059`, `rule.13.ReadPtr-Raw@L46071`, `rule.13.WritePtr-Raw@L46087`, `rule.13.ReadPtr-Raw-Invalid@L46103`
- `rule.13.WritePtr-Raw-Imm@L46119`, `rule.13.WritePtr-Raw-Invalid@L46135`, `rule.13.Size-RawPtr@L46153`, `rule.13.Align-RawPtr@L46169`, `rule.13.Layout-RawPtr@L46185`, `def.RawPointerValidValue@L46201`, `def.RawPointerValueBits@L46213`, `req.RawPointerLowering@L46224`
- `diag.RawPointerTypes@L46238`, `grammar.FunctionTypes.Syntax@L46254`, `req.FunctionTypeTrailingComma@L46270`, `rule.13.Parse-Func-Type@L46284`, `rule.13.Parse-ParamType-Move@L46300`, `rule.13.Parse-ParamType-Plain@L46316`, `rule.13.Parse-ParamTypeList-Empty@L46332`, `rule.13.Parse-ParamTypeList-Cons@L46348`
- `rule.13.Parse-ParamTypeListTail-End@L46364`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46380`, `rule.13.Parse-ParamTypeListTail-Cons@L46396`, `def.FunctionTypes.AST@L46414`, `rule.13.WF-Func@L46428`, `rule.13.T-Equiv-Func@L46444`, `rule.13.Sub-Func@L46460`, `rule.13.T-Proc-As-Value@L46476`
- `diag.FunctionTypeCalls@L46492`, `def.FunctionRuntimeValue@L46506`, `rule.13.EvalSigma-Call-Proc@L46518`, `req.NamedProceduresFirstClass@L46534`, `rule.13.Size-Func@L46548`, `rule.13.Align-Func@L46564`, `rule.13.Layout-Func@L46580`, `req.FunctionTypeCallLowering@L46596`
- `diag.FunctionTypes@L46610`, `grammar.ClosureTypes.Syntax@L46626`, `req.ClosureParamUnionParentheses@L46643`, `rule.13.Parse-Closure-Type@L46657`, `rule.13.Parse-Closure-Type-Empty@L46673`, `rule.13.Parse-ClosureParamType-Grouped@L46689`, `rule.13.Parse-ClosureParamType-Plain@L46705`, `rule.13.Parse-ClosureParamTypeList-Empty@L46721`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46737`, `rule.13.Parse-ClosureParamTypeListTail-End@L46753`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46769`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46785`, `rule.13.Parse-ClosureDepsOpt-None@L46801`, `rule.13.Parse-ClosureDepsOpt-Some@L46817`, `rule.13.Parse-SharedDepList-Empty@L46833`, `rule.13.Parse-SharedDepList-Single@L46849`
- `rule.13.Parse-SharedDepList-Cons@L46865`, `rule.13.Parse-SharedDep@L46881`, `def.TypeClosureAst@L46899`, `def.ClosureDepsOpt@L46911`, `req.ClosureTypeOwnershipBoundaries@L46923`, `rule.13.WF-Closure@L46937`, `rule.13.T-Equiv-Closure@L46953`, `rule.13.Sub-Closure@L46969`
- `req.ClosureExpressionOwnership@L46985`, `def.ClosureRuntimeValue@L46999`, `req.ClosureOperationOwnership@L47011`, `def.ClosureLoweringRep@L47025`, `rule.13.Size-Closure@L47037`, `rule.13.Align-Closure@L47053`, `rule.13.Layout-Closure@L47069`, `req.ClosureLoweringOwnership@L47085`
- `diag.ClosureTypes@L47099`, `diagnostics.ModalPointerSupplement@L47111`

#### `spec.abstraction-polymorphism`

Count: 328 total; 327 required; 0 recommended; 0 informative. Ledger line span: L46800-L51908.

- `grammar.GenericParamsAndArgsSyntax@L47159`, `req.GenericArgsTrailingComma@L47177`, `req.GenericParamInlineBoundsClassOnly@L47189`, `rule.14.Parse-GenericArgs@L47203`, `rule.14.Parse-GenericArgsOpt-None@L47219`, `rule.14.Parse-GenericArgsOpt-Yes@L47235`, `rule.14.Parse-GenericParamsOpt-None@L47251`, `rule.14.Parse-GenericParamsOpt-Yes@L47267`
- `rule.14.Parse-GenericParams@L47283`, `rule.14.Parse-TypeParamTail-End@L47299`, `rule.14.Parse-TypeParamTail-Cons@L47315`, `rule.14.Parse-TypeParam@L47331`, `rule.14.Parse-TypeBoundsOpt-None@L47347`, `rule.14.Parse-TypeBoundsOpt-Yes@L47363`, `rule.14.Parse-ClassBoundList-Cons@L47379`, `rule.14.Parse-ClassBoundListTail-End@L47395`
- `rule.14.Parse-ClassBoundListTail-Cons@L47411`, `rule.14.Parse-ClassBound@L47427`, `rule.14.Parse-TypeDefaultOpt-None@L47443`, `rule.14.Parse-TypeDefaultOpt-Yes@L47459`, `rule.14.Parse-PredicateClauseOpt-None@L47475`, `rule.14.Parse-PredicateClauseOpt-Yes@L47491`, `rule.14.Parse-PredicateReqList-Cons@L47507`, `rule.14.Parse-PredicateReqListTail-End@L47523`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47539`, `rule.14.Parse-PredicateReqListTail-Cons@L47555`, `def.PredicateNameParserSet@L47571`, `rule.14.Parse-PredicateReq-Predicate@L47583`, `rule.14.Parse-PredicateReq-Err@L47599`, `def.VarianceSet@L47617`, `def.GenericParamAst@L47629`, `def.PredicateClauseAst@L47643`
- `def.GenericParamHelpers@L47657`, `def.GenericDefaultWellFormedness@L47676`, `rule.14.WF-Generic-Param@L47690`, `def.DefaultArgs@L47706`, `rule.14.PredicateReq-WF-Predicate@L47723`, `def.PredicateClauseWellFormedness@L47739`, `def.PredOk@L47751`, `rule.14.T-Constraint-Sat@L47766`
- `rule.14.PredicateReq-Predicate@L47782`, `def.PredicateClauseSubstitutionOk@L47798`, `req.GenericBoundsAndPredicatesConjunctive@L47810`, `conformance.GenericParamsNoRuntimeSemantics@L47824`, `conformance.GenericParamsLoweringInputsOnly@L47838`, `diag.GenericParametersAndArguments@L47852`, `grammar.GenericProceduresAndTypesSyntax@L47868`, `req.GenericParamsNominalOwnerChapters@L47884`
- `req.GenericDeclarationParsingDelegated@L47898`, `def.CallTypeArgsStart@L47910`, `rule.14.Postfix-Call-TypeArgs@L47922`, `def.GenericDeclarationAstExtensions@L47940`, `def.GenericApplyAst@L47957`, `def.GenericDeclarationAccessors@L47971`, `rule.14.WF-Generic-Proc@L47986`, `def.GenericCalleeProc@L48002`
- `def.GenericInferenceFreshArgs@L48016`, `def.InferTypeArgs@L48029`, `rule.14.GenericCallInference@L48047`, `rule.14.T-Generic-Call@L48078`, `rule.14.Generic-Call-ArgCount-Err@L48101`, `rule.14.WF-Path-Generic-Err@L48117`, `rule.14.WF-Apply@L48133`, `rule.14.WF-Apply-ArgCount-Err@L48149`
- `req.GenericCallInferenceElaboration@L48165`, `conformance.GenericInstantiationDynamicElaboration@L48179`, `conformance.GenericMonomorphicInstantiationsDistinct@L48191`, `def.MonomorphizationSpecialization@L48205`, `req.GenericProcedureCallLowering@L48219`, `req.GenericInstantiationIndependentLowering@L48231`, `req.GenericInfiniteMonomorphizationRejected@L48243`, `req.GenericInstantiationDepthLimit@L48255`
- `req.GenericNominalSizeAlignSubstitutedBody@L48267`, `diag.GenericProceduresAndTypes@L48281`, `grammar.ClassesSyntax@L48297`, `req.AssociatedTypeSyntaxCanonicalOwner@L48314`, `rule.14.Parse-Class@L48328`, `rule.14.Parse-Superclass-None@L48344`, `rule.14.Parse-Superclass-Yes@L48360`, `rule.14.Parse-SuperclassBounds-Cons@L48376`
- `rule.14.Parse-SuperclassBoundsTail-End@L48392`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48408`, `rule.14.Parse-ClassBody@L48424`, `rule.14.Parse-ClassItemList-End@L48440`, `rule.14.Parse-ClassItemList-Cons@L48456`, `rule.14.Parse-ClassItem-Method@L48472`, `rule.14.Parse-ClassItem-Field@L48488`, `rule.14.Parse-ClassItem-AbstractState@L48504`
- `rule.14.Parse-ClassMethodBody-Concrete@L48520`, `rule.14.Parse-ClassMethodBody-Abstract@L48536`, `def.ClassDeclAst@L48554`, `def.ClassItemAst@L48567`, `def.ClassMethodAbstractConcretePredicates@L48584`, `def.ClassMemberCollections@L48597`, `def.ClassMethodReturnType@L48613`, `def.SelfVar@L48626`
- `def.DistinctDisjoint@L48640`, `rule.14.WF-ClassPath@L48653`, `rule.14.WF-ClassPath-Err@L48669`, `def.SubstSelf@L48685`, `def.ReceiverTypeHelpers@L48711`, `def.Supers@L48740`, `rule.14.T-Superclass@L48752`, `def.ClassLinearizationMergeHelpers@L48768`
- `rule.14.Lin-Base@L48788`, `rule.14.Merge-Empty@L48804`, `rule.14.Merge-Step@L48820`, `rule.14.Merge-Fail@L48836`, `rule.14.Lin-Ok@L48852`, `rule.14.Lin-Fail@L48868`, `rule.14.Superclass-Cycle@L48884`, `def.LinearizeHeadInvariant@L48900`
- `def.EffectiveClassMembers@L48912`, `def.FirstByName@L48925`, `rule.14.EffMethods-Conflict@L48942`, `def.FieldSig@L48958`, `def.FirstFieldByName@L48970`, `rule.14.EffFields-Conflict@L48987`, `def.SelfTypeClass@L49003`, `rule.14.WF-Class-Method@L49015`
- `rule.14.T-Class-Method-Body-Abstract@L49031`, `rule.14.T-Class-Method-Body@L49047`, `rule.14.WF-Class@L49063`, `conformance.ClassDeclarationsNoRuntimeActions@L49081`, `req.ClassMethodLowering@L49095`, `req.ClassDefaultMethodAndVtableOwnership@L49107`, `diag.Classes@L49121`, `grammar.ImplementationsSyntax@L49137`
- `req.NoStandaloneImplementationBlocks@L49152`, `rule.14.Parse-Implements-None@L49166`, `rule.14.Parse-Implements-Yes@L49182`, `rule.14.Parse-ClassList-Cons@L49198`, `rule.14.Parse-ClassListTail-End@L49214`, `rule.14.Parse-ClassListTail-Comma@L49230`, `def.ImplementsAccessor@L49248`, `req.SubtypeOperatorImplementationMeaning@L49260`
- `req.ImplementationsConcreteOwnerOnly@L49275`, `def.ImplementerFields@L49287`, `def.ImplementerMethods@L49300`, `def.MethodByName@L49313`, `def.ClassEffectiveTables@L49326`, `def.ImplementationOrphanRule@L49339`, `def.DefaultMethodPredicates@L49355`, `rule.14.Impl-Abstract-Method@L49368`
- `rule.14.Impl-Missing-Method@L49384`, `rule.14.Impl-AssocType-Missing@L49400`, `rule.14.Impl-Sig-Err@L49416`, `rule.14.Override-Abstract-Err@L49432`, `rule.14.Impl-Concrete-Default@L49448`, `rule.14.Impl-Concrete-Override@L49464`, `rule.14.Override-Missing-Err@L49480`, `rule.14.Impl-Sig-Err-Concrete@L49496`
- `rule.14.Override-NoConcrete@L49512`, `rule.14.Impl-Field@L49528`, `rule.14.Impl-Field-Missing@L49544`, `rule.14.Impl-Field-Type-Err@L49560`, `rule.14.Impl-Coherence-Err@L49576`, `rule.14.Impl-Orphan-Err@L49592`, `rule.14.WF-Impl@L49608`, `rule.14.ImplementationSubtypeRelation@L49624`
- `req.14.ModalClassImplementationRequiresModalType@L49637`, `req.14.DuplicateClassImplementationForbidden@L49650`, `req.14.ImplementationOrphanRequirement@L49663`, `req.14.ImplementationsNoAdditionalRuntimeState@L49678`, `req.14.ImplementationBodyLowering@L49693`, `diag.14.Implementations@L49708`, `grammar.14.AssociatedType@L49725`, `req.14.AssociatedTypeEqualsMeaning@L49740`
- `rule.14.Parse-ClassItem-AssociatedType@L49755`, `rule.14.Parse-AssocTypeOpt-None@L49771`, `rule.14.Parse-AssocTypeOpt-Yes@L49787`, `rule.14.Parse-AssocTypeDefaultOpt@L49803`, `rule.14.Parse-RecordMember-AssociatedType@L49819`, `def.14.AssociatedTypeDeclAst@L49837`, `def.14.AssociatedTypeAstMembership@L49850`, `def.14.AssociatedTypeClassAbstractDefaulted@L49864`
- `def.14.AssocTypeItemsAndNames@L49877`, `def.14.AssocTypeDefault@L49891`, `def.14.ImplAssocType@L49905`, `def.14.AbstractAssociatedTypeNames@L49919`, `def.14.AssocTypeBinding@L49932`, `def.14.AssocTypeBindsPredicate@L49947`, `req.14.GenericParametersAssociatedTypesSupplySites@L49962`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49975`
- `req.14.ImplementationAssociatedTypeBoundForm@L49988`, `def.14.AssociatedTypeLookupOrder@L50001`, `rule.14.T-Alias-Equiv@L50018`, `req.14.AssociatedTypesNoRuntimeSemantics@L50036`, `req.14.AssociatedTypeErasureLowering@L50051`, `diag.14.AssociatedTypes@L50066`, `grammar.14.DynamicClassObjects@L50083`, `req.14.DynamicMethodCallSurfaceSyntax@L50099`
- `rule.14.Parse-Dynamic-Type@L50114`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50130`, `def.14.TypeDynamicAst@L50145`, `def.14.DynamicClassLayoutFields@L50158`, `def.14.DynamicClassRuntimeValue@L50172`, `def.14.SelfOccurs@L50185`, `def.14.DynamicDispatchEligibility@L50212`, `rule.14.WF-Dynamic@L50230`
- `rule.14.WF-Dynamic-Err@L50246`, `rule.14.T-Equiv-Dynamic@L50262`, `rule.14.T-Dynamic-Form@L50278`, `rule.14.Dynamic-NonDispatchable@L50294`, `def.14.LookupMethod@L50310`, `rule.14.T-Dynamic-MethodCall@L50325`, `rule.14.LookupClassMethod-NotFound@L50341`, `req.14.DynamicDispatchDispatchableClassesOnly@L50357`
- `def.14.DynamicValueType@L50372`, `rule.14.Eval-Dynamic-Form@L50385`, `rule.14.Eval-Dynamic-Form-Ctrl@L50401`, `def.14.DynamicDispatchSelection@L50417`, `def.14.DynamicMethodTarget@L50432`, `rule.14.Layout-DynamicClass@L50447`, `rule.14.Size-DynamicClass@L50462`, `rule.14.Align-DynamicClass@L50478`
- `rule.14.ABI-Dynamic@L50494`, `def.14.DynamicValueBits@L50510`, `def.14.DynamicDispatchLoweringJudgements@L50523`, `rule.14.DispatchSym-Impl@L50537`, `rule.14.DispatchSym-Default-None@L50553`, `rule.14.DispatchSym-Default-Mismatch@L50569`, `rule.14.VTable-Order@L50585`, `rule.14.VSlot-Entry@L50601`
- `rule.14.Lower-Dynamic-Form@L50617`, `rule.14.Lower-DynCall@L50633`, `rule.14.EmitVTable-Decl@L50649`, `diag.14.DynamicClassObjects@L50667`, `grammar.14.OpaqueTypes@L50684`, `req.14.OpaqueTypesComposeAsTypeForms@L50699`, `rule.14.Parse-Opaque-Type@L50714`, `def.14.TypeOpaqueAst@L50732`
- `def.14.TypeOpaqueForm@L50745`, `rule.14.WF-Opaque@L50760`, `rule.14.WF-Opaque-Err@L50776`, `rule.14.T-Equiv-Opaque@L50792`, `rule.14.T-Opaque-Return@L50808`, `rule.14.T-Opaque-Project@L50824`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50840`, `req.14.OpaqueTypesNoRuntimeWrapper@L50855`
- `req.14.OpaqueTypesLowerAsConcrete@L50870`, `diag.14.OpaqueTypes@L50885`, `grammar.14.RefinementTypes@L50902`, `req.14.RefinementSelfBinding@L50919`, `rule.14.Parse-RefinementOpt-None@L50934`, `rule.14.Parse-RefinementOpt-Yes@L50950`, `rule.14.ParsePredicateExpr@L50966`, `def.14.TypeRefineAst@L50981`
- `def.14.TypeRefineForm@L50994`, `def.14.PredicateEquiv@L51007`, `rule.14.T-Equiv-Refine@L51022`, `rule.14.T-Equiv-Refine-Norm@L51038`, `rule.14.WF-Refine-Type@L51054`, `rule.14.T-Refine-Intro@L51070`, `rule.14.T-Refine-Elim@L51086`, `rule.14.RefinementSubtypeBase@L51102`
- `rule.14.RefinementSubtypeImplication@L51117`, `req.14.RefinementDecidablePredicateFragment@L51132`, `req.14.RefinementStaticDefaultDynamicFallback@L51145`, `req.14.RefinementRuntimeRepresentationAndPanic@L51160`, `rule.14.LLVMTy-Refine@L51175`, `req.14.RefinementRuntimeCheckLowering@L51191`, `diag.14.RefinementTypes@L51206`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51223`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51238`, `def.14.CapClassSet@L51253`, `def.14.CapType@L51266`, `def.14.FileSystemInterface@L51279`, `def.14.NetworkInterface@L51310`, `def.14.HeapAllocatorInterface@L51326`, `def.14.FileKindDecl@L51344`, `def.14.IoErrorDecl@L51362`
- `def.14.DirEntryDecl@L51383`, `def.14.AllocationErrorDecl@L51401`, `def.14.ContextDecl@L51418`, `def.14.SystemDecl@L51444`, `def.14.ExecutionDomainSupportDecls@L51474`, `def.14.ReactorDecl@L51493`, `def.14.CapMethodSig@L51513`, `def.14.CapRecv@L51530`
- `def.14.CapabilityLoweringSupport@L51546`, `req.14.CapabilityClassesOrdinaryClasses@L51563`, `req.14.CapabilityClassesGenericBounds@L51576`, `req.14.CapabilityClassNamesReserved@L51589`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51602`, `rule.14.AllocRaw-Unsafe-Err@L51615`, `rule.14.DeallocRaw-Unsafe-Err@L51631`, `def.14.BuiltinTypesFS@L51647`
- `def.14.BuiltinDeclLookup@L51660`, `def.14.BuiltinTypeEnvironment@L51679`, `def.14.BuiltInContext@L51699`, `def.14.ContextBundleFieldType@L51712`, `def.14.ContextBundleType@L51732`, `def.14.ContextBundleFieldValue@L51746`, `def.14.ContextDomainValue@L51766`, `def.14.ContextBundleBuild@L51779`
- `def.14.AllocErrorVal@L51795`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51810`, `req.14.CapabilityBuiltinMethodLowering@L51825`, `diag.14.CapabilityClasses@L51840`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51857`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51872`, `def.14.FoundationalClassName@L51887`, `def.14.FoundationalJudgements@L51900`
- `def.14.HasCloneDropMethod@L51916`, `def.14.CloneDropTypePredicates@L51930`, `def.14.FoundationalImplementationPredicates@L51944`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51964`, `rule.14.BitcopyDrop-Ok@L51977`, `rule.14.BitcopyDrop-Conflict@L51993`, `def.14.BitcopyType@L52009`, `def.14.BitcopyTypeCore@L52022`
- `def.14.BuiltinBitcopyType@L52045`, `def.14.BuiltinDropCloneType@L52076`, `def.14.BuiltinFoundationalClassSignatures@L52090`, `req.14.EqLaws@L52109`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52122`, `req.14.IteratorNextContract@L52135`, `req.14.StepPartialInverseContract@L52148`, `req.14.DropCloneDynamicSemantics@L52163`
- `req.14.HasherDynamicSemantics@L52176`, `req.14.IntegerStepDynamicSemantics@L52189`, `req.14.CharStepDynamicSemantics@L52202`, `req.14.FoundationalIntrinsicCallLowering@L52217`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52230`, `diag.14.FoundationalClasses@L52245`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52260`, `diag-table.14.RefinementPolymorphismDiagnostics@L52273`
- `grammar.GenericParamsAndArgsSyntax@L47159`, `req.GenericArgsTrailingComma@L47177`, `req.GenericParamInlineBoundsClassOnly@L47189`, `rule.14.Parse-GenericArgs@L47203`, `rule.14.Parse-GenericArgsOpt-None@L47219`, `rule.14.Parse-GenericArgsOpt-Yes@L47235`, `rule.14.Parse-GenericParamsOpt-None@L47251`, `rule.14.Parse-GenericParamsOpt-Yes@L47267`
- `rule.14.Parse-GenericParams@L47283`, `rule.14.Parse-TypeParamTail-End@L47299`, `rule.14.Parse-TypeParamTail-Cons@L47315`, `rule.14.Parse-TypeParam@L47331`, `rule.14.Parse-TypeBoundsOpt-None@L47347`, `rule.14.Parse-TypeBoundsOpt-Yes@L47363`, `rule.14.Parse-ClassBoundList-Cons@L47379`, `rule.14.Parse-ClassBoundListTail-End@L47395`
- `rule.14.Parse-ClassBoundListTail-Cons@L47411`, `rule.14.Parse-ClassBound@L47427`, `rule.14.Parse-TypeDefaultOpt-None@L47443`, `rule.14.Parse-TypeDefaultOpt-Yes@L47459`, `rule.14.Parse-PredicateClauseOpt-None@L47475`, `rule.14.Parse-PredicateClauseOpt-Yes@L47491`, `rule.14.Parse-PredicateReqList-Cons@L47507`, `rule.14.Parse-PredicateReqListTail-End@L47523`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47539`, `rule.14.Parse-PredicateReqListTail-Cons@L47555`, `def.PredicateNameParserSet@L47571`, `rule.14.Parse-PredicateReq-Predicate@L47583`, `rule.14.Parse-PredicateReq-Err@L47599`, `def.VarianceSet@L47617`, `def.GenericParamAst@L47629`, `def.PredicateClauseAst@L47643`
- `def.GenericParamHelpers@L47657`, `def.GenericDefaultWellFormedness@L47676`, `rule.14.WF-Generic-Param@L47690`, `def.DefaultArgs@L47706`, `rule.14.PredicateReq-WF-Predicate@L47723`, `def.PredicateClauseWellFormedness@L47739`, `def.PredOk@L47751`, `rule.14.T-Constraint-Sat@L47766`
- `rule.14.PredicateReq-Predicate@L47782`, `def.PredicateClauseSubstitutionOk@L47798`, `req.GenericBoundsAndPredicatesConjunctive@L47810`, `conformance.GenericParamsNoRuntimeSemantics@L47824`, `conformance.GenericParamsLoweringInputsOnly@L47838`, `diag.GenericParametersAndArguments@L47852`, `grammar.GenericProceduresAndTypesSyntax@L47868`, `req.GenericParamsNominalOwnerChapters@L47884`
- `req.GenericDeclarationParsingDelegated@L47898`, `def.CallTypeArgsStart@L47910`, `rule.14.Postfix-Call-TypeArgs@L47922`, `def.GenericDeclarationAstExtensions@L47940`, `def.GenericApplyAst@L47957`, `def.GenericDeclarationAccessors@L47971`, `rule.14.WF-Generic-Proc@L47986`, `def.GenericCalleeProc@L48002`
- `def.GenericInferenceFreshArgs@L48016`, `def.InferTypeArgs@L48029`, `rule.14.GenericCallInference@L48047`, `rule.14.T-Generic-Call@L48078`, `rule.14.Generic-Call-ArgCount-Err@L48101`, `rule.14.WF-Path-Generic-Err@L48117`, `rule.14.WF-Apply@L48133`, `rule.14.WF-Apply-ArgCount-Err@L48149`
- `req.GenericCallInferenceElaboration@L48165`, `conformance.GenericInstantiationDynamicElaboration@L48179`, `conformance.GenericMonomorphicInstantiationsDistinct@L48191`, `def.MonomorphizationSpecialization@L48205`, `req.GenericProcedureCallLowering@L48219`, `req.GenericInstantiationIndependentLowering@L48231`, `req.GenericInfiniteMonomorphizationRejected@L48243`, `req.GenericInstantiationDepthLimit@L48255`
- `req.GenericNominalSizeAlignSubstitutedBody@L48267`, `diag.GenericProceduresAndTypes@L48281`, `grammar.ClassesSyntax@L48297`, `req.AssociatedTypeSyntaxCanonicalOwner@L48314`, `rule.14.Parse-Class@L48328`, `rule.14.Parse-Superclass-None@L48344`, `rule.14.Parse-Superclass-Yes@L48360`, `rule.14.Parse-SuperclassBounds-Cons@L48376`
- `rule.14.Parse-SuperclassBoundsTail-End@L48392`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48408`, `rule.14.Parse-ClassBody@L48424`, `rule.14.Parse-ClassItemList-End@L48440`, `rule.14.Parse-ClassItemList-Cons@L48456`, `rule.14.Parse-ClassItem-Method@L48472`, `rule.14.Parse-ClassItem-Field@L48488`, `rule.14.Parse-ClassItem-AbstractState@L48504`
- `rule.14.Parse-ClassMethodBody-Concrete@L48520`, `rule.14.Parse-ClassMethodBody-Abstract@L48536`, `def.ClassDeclAst@L48554`, `def.ClassItemAst@L48567`, `def.ClassMethodAbstractConcretePredicates@L48584`, `def.ClassMemberCollections@L48597`, `def.ClassMethodReturnType@L48613`, `def.SelfVar@L48626`
- `def.DistinctDisjoint@L48640`, `rule.14.WF-ClassPath@L48653`, `rule.14.WF-ClassPath-Err@L48669`, `def.SubstSelf@L48685`, `def.ReceiverTypeHelpers@L48711`, `def.Supers@L48740`, `rule.14.T-Superclass@L48752`, `def.ClassLinearizationMergeHelpers@L48768`
- `rule.14.Lin-Base@L48788`, `rule.14.Merge-Empty@L48804`, `rule.14.Merge-Step@L48820`, `rule.14.Merge-Fail@L48836`, `rule.14.Lin-Ok@L48852`, `rule.14.Lin-Fail@L48868`, `rule.14.Superclass-Cycle@L48884`, `def.LinearizeHeadInvariant@L48900`
- `def.EffectiveClassMembers@L48912`, `def.FirstByName@L48925`, `rule.14.EffMethods-Conflict@L48942`, `def.FieldSig@L48958`, `def.FirstFieldByName@L48970`, `rule.14.EffFields-Conflict@L48987`, `def.SelfTypeClass@L49003`, `rule.14.WF-Class-Method@L49015`
- `rule.14.T-Class-Method-Body-Abstract@L49031`, `rule.14.T-Class-Method-Body@L49047`, `rule.14.WF-Class@L49063`, `conformance.ClassDeclarationsNoRuntimeActions@L49081`, `req.ClassMethodLowering@L49095`, `req.ClassDefaultMethodAndVtableOwnership@L49107`, `diag.Classes@L49121`, `grammar.ImplementationsSyntax@L49137`
- `req.NoStandaloneImplementationBlocks@L49152`, `rule.14.Parse-Implements-None@L49166`, `rule.14.Parse-Implements-Yes@L49182`, `rule.14.Parse-ClassList-Cons@L49198`, `rule.14.Parse-ClassListTail-End@L49214`, `rule.14.Parse-ClassListTail-Comma@L49230`, `def.ImplementsAccessor@L49248`, `req.SubtypeOperatorImplementationMeaning@L49260`
- `req.ImplementationsConcreteOwnerOnly@L49275`, `def.ImplementerFields@L49287`, `def.ImplementerMethods@L49300`, `def.MethodByName@L49313`, `def.ClassEffectiveTables@L49326`, `def.ImplementationOrphanRule@L49339`, `def.DefaultMethodPredicates@L49355`, `rule.14.Impl-Abstract-Method@L49368`
- `rule.14.Impl-Missing-Method@L49384`, `rule.14.Impl-AssocType-Missing@L49400`, `rule.14.Impl-Sig-Err@L49416`, `rule.14.Override-Abstract-Err@L49432`, `rule.14.Impl-Concrete-Default@L49448`, `rule.14.Impl-Concrete-Override@L49464`, `rule.14.Override-Missing-Err@L49480`, `rule.14.Impl-Sig-Err-Concrete@L49496`
- `rule.14.Override-NoConcrete@L49512`, `rule.14.Impl-Field@L49528`, `rule.14.Impl-Field-Missing@L49544`, `rule.14.Impl-Field-Type-Err@L49560`, `rule.14.Impl-Coherence-Err@L49576`, `rule.14.Impl-Orphan-Err@L49592`, `rule.14.WF-Impl@L49608`, `rule.14.ImplementationSubtypeRelation@L49624`
- `req.14.ModalClassImplementationRequiresModalType@L49637`, `req.14.DuplicateClassImplementationForbidden@L49650`, `req.14.ImplementationOrphanRequirement@L49663`, `req.14.ImplementationsNoAdditionalRuntimeState@L49678`, `req.14.ImplementationBodyLowering@L49693`, `diag.14.Implementations@L49708`, `grammar.14.AssociatedType@L49725`, `req.14.AssociatedTypeEqualsMeaning@L49740`
- `rule.14.Parse-ClassItem-AssociatedType@L49755`, `rule.14.Parse-AssocTypeOpt-None@L49771`, `rule.14.Parse-AssocTypeOpt-Yes@L49787`, `rule.14.Parse-AssocTypeDefaultOpt@L49803`, `rule.14.Parse-RecordMember-AssociatedType@L49819`, `def.14.AssociatedTypeDeclAst@L49837`, `def.14.AssociatedTypeAstMembership@L49850`, `def.14.AssociatedTypeClassAbstractDefaulted@L49864`
- `def.14.AssocTypeItemsAndNames@L49877`, `def.14.AssocTypeDefault@L49891`, `def.14.ImplAssocType@L49905`, `def.14.AbstractAssociatedTypeNames@L49919`, `def.14.AssocTypeBinding@L49932`, `def.14.AssocTypeBindsPredicate@L49947`, `req.14.GenericParametersAssociatedTypesSupplySites@L49962`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49975`
- `req.14.ImplementationAssociatedTypeBoundForm@L49988`, `def.14.AssociatedTypeLookupOrder@L50001`, `rule.14.T-Alias-Equiv@L50018`, `req.14.AssociatedTypesNoRuntimeSemantics@L50036`, `req.14.AssociatedTypeErasureLowering@L50051`, `diag.14.AssociatedTypes@L50066`, `grammar.14.DynamicClassObjects@L50083`, `req.14.DynamicMethodCallSurfaceSyntax@L50099`
- `rule.14.Parse-Dynamic-Type@L50114`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50130`, `def.14.TypeDynamicAst@L50145`, `def.14.DynamicClassLayoutFields@L50158`, `def.14.DynamicClassRuntimeValue@L50172`, `def.14.SelfOccurs@L50185`, `def.14.DynamicDispatchEligibility@L50212`, `rule.14.WF-Dynamic@L50230`
- `rule.14.WF-Dynamic-Err@L50246`, `rule.14.T-Equiv-Dynamic@L50262`, `rule.14.T-Dynamic-Form@L50278`, `rule.14.Dynamic-NonDispatchable@L50294`, `def.14.LookupMethod@L50310`, `rule.14.T-Dynamic-MethodCall@L50325`, `rule.14.LookupClassMethod-NotFound@L50341`, `req.14.DynamicDispatchDispatchableClassesOnly@L50357`
- `def.14.DynamicValueType@L50372`, `rule.14.Eval-Dynamic-Form@L50385`, `rule.14.Eval-Dynamic-Form-Ctrl@L50401`, `def.14.DynamicDispatchSelection@L50417`, `def.14.DynamicMethodTarget@L50432`, `rule.14.Layout-DynamicClass@L50447`, `rule.14.Size-DynamicClass@L50462`, `rule.14.Align-DynamicClass@L50478`
- `rule.14.ABI-Dynamic@L50494`, `def.14.DynamicValueBits@L50510`, `def.14.DynamicDispatchLoweringJudgements@L50523`, `rule.14.DispatchSym-Impl@L50537`, `rule.14.DispatchSym-Default-None@L50553`, `rule.14.DispatchSym-Default-Mismatch@L50569`, `rule.14.VTable-Order@L50585`, `rule.14.VSlot-Entry@L50601`
- `rule.14.Lower-Dynamic-Form@L50617`, `rule.14.Lower-DynCall@L50633`, `rule.14.EmitVTable-Decl@L50649`, `diag.14.DynamicClassObjects@L50667`, `grammar.14.OpaqueTypes@L50684`, `req.14.OpaqueTypesComposeAsTypeForms@L50699`, `rule.14.Parse-Opaque-Type@L50714`, `def.14.TypeOpaqueAst@L50732`
- `def.14.TypeOpaqueForm@L50745`, `rule.14.WF-Opaque@L50760`, `rule.14.WF-Opaque-Err@L50776`, `rule.14.T-Equiv-Opaque@L50792`, `rule.14.T-Opaque-Return@L50808`, `rule.14.T-Opaque-Project@L50824`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50840`, `req.14.OpaqueTypesNoRuntimeWrapper@L50855`
- `req.14.OpaqueTypesLowerAsConcrete@L50870`, `diag.14.OpaqueTypes@L50885`, `grammar.14.RefinementTypes@L50902`, `req.14.RefinementSelfBinding@L50919`, `rule.14.Parse-RefinementOpt-None@L50934`, `rule.14.Parse-RefinementOpt-Yes@L50950`, `rule.14.ParsePredicateExpr@L50966`, `def.14.TypeRefineAst@L50981`
- `def.14.TypeRefineForm@L50994`, `def.14.PredicateEquiv@L51007`, `rule.14.T-Equiv-Refine@L51022`, `rule.14.T-Equiv-Refine-Norm@L51038`, `rule.14.WF-Refine-Type@L51054`, `rule.14.T-Refine-Intro@L51070`, `rule.14.T-Refine-Elim@L51086`, `rule.14.RefinementSubtypeBase@L51102`
- `rule.14.RefinementSubtypeImplication@L51117`, `req.14.RefinementDecidablePredicateFragment@L51132`, `req.14.RefinementStaticDefaultDynamicFallback@L51145`, `req.14.RefinementRuntimeRepresentationAndPanic@L51160`, `rule.14.LLVMTy-Refine@L51175`, `req.14.RefinementRuntimeCheckLowering@L51191`, `diag.14.RefinementTypes@L51206`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51223`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51238`, `def.14.CapClassSet@L51253`, `def.14.CapType@L51266`, `def.14.FileSystemInterface@L51279`, `def.14.NetworkInterface@L51310`, `def.14.HeapAllocatorInterface@L51326`, `def.14.FileKindDecl@L51344`, `def.14.IoErrorDecl@L51362`
- `def.14.DirEntryDecl@L51383`, `def.14.AllocationErrorDecl@L51401`, `def.14.ContextDecl@L51418`, `def.14.SystemDecl@L51444`, `def.14.ExecutionDomainSupportDecls@L51474`, `def.14.ReactorDecl@L51493`, `def.14.CapMethodSig@L51513`, `def.14.CapRecv@L51530`
- `def.14.CapabilityLoweringSupport@L51546`, `req.14.CapabilityClassesOrdinaryClasses@L51563`, `req.14.CapabilityClassesGenericBounds@L51576`, `req.14.CapabilityClassNamesReserved@L51589`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51602`, `rule.14.AllocRaw-Unsafe-Err@L51615`, `rule.14.DeallocRaw-Unsafe-Err@L51631`, `def.14.BuiltinTypesFS@L51647`
- `def.14.BuiltinDeclLookup@L51660`, `def.14.BuiltinTypeEnvironment@L51679`, `def.14.BuiltInContext@L51699`, `def.14.ContextBundleFieldType@L51712`, `def.14.ContextBundleType@L51732`, `def.14.ContextBundleFieldValue@L51746`, `def.14.ContextDomainValue@L51766`, `def.14.ContextBundleBuild@L51779`
- `def.14.AllocErrorVal@L51795`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51810`, `req.14.CapabilityBuiltinMethodLowering@L51825`, `diag.14.CapabilityClasses@L51840`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51857`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51872`, `def.14.FoundationalClassName@L51887`, `def.14.FoundationalJudgements@L51900`
- `def.14.HasCloneDropMethod@L51916`, `def.14.CloneDropTypePredicates@L51930`, `def.14.FoundationalImplementationPredicates@L51944`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51964`, `rule.14.BitcopyDrop-Ok@L51977`, `rule.14.BitcopyDrop-Conflict@L51993`, `def.14.BitcopyType@L52009`, `def.14.BitcopyTypeCore@L52022`
- `def.14.BuiltinBitcopyType@L52045`, `def.14.BuiltinDropCloneType@L52076`, `def.14.BuiltinFoundationalClassSignatures@L52090`, `req.14.EqLaws@L52109`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52122`, `req.14.IteratorNextContract@L52135`, `req.14.StepPartialInverseContract@L52148`, `req.14.DropCloneDynamicSemantics@L52163`
- `req.14.HasherDynamicSemantics@L52176`, `req.14.IntegerStepDynamicSemantics@L52189`, `req.14.CharStepDynamicSemantics@L52202`, `req.14.FoundationalIntrinsicCallLowering@L52217`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52230`, `diag.14.FoundationalClasses@L52245`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52260`, `diag-table.14.RefinementPolymorphismDiagnostics@L52273`

#### `spec.procedures-contracts`

Count: 283 total; 283 required; 0 recommended; 0 informative. Ledger line span: L51972-L56430.

- `grammar.15.ProcedureDeclarations@L52337`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52355`, `rule.15.Parse-Procedure@L52370`, `rule.15.Parse-Signature@L52386`, `rule.15.Parse-ParamList-Empty@L52402`, `rule.15.Parse-ParamList-Cons@L52418`, `rule.15.Parse-Param@L52434`, `rule.15.Parse-ParamMode-None@L52450`
- `rule.15.Parse-ParamMode-Move@L52466`, `rule.15.Parse-ParamTail-End@L52482`, `rule.15.Parse-ParamTail-TrailingComma@L52498`, `rule.15.Parse-ParamTail-Comma@L52514`, `rule.15.Parse-ReturnOpt-None@L52530`, `rule.15.Parse-ReturnOpt-Arrow@L52546`, `def.15.ProcedureDeclAst@L52564`, `def.15.ParamAst@L52577`
- `def.15.ParamNamesAndBinds@L52590`, `def.15.ProcReturn@L52604`, `def.15.BodyReturnType@L52619`, `def.15.ExplicitReturn@L52634`, `def.15.ReturnAnnOk@L52649`, `rule.15.WF-ProcedureDecl@L52662`, `def.15.DeclTyping@L52678`, `def.15.ProvBindCheck@L52693`
- `def.15.DeclTypingItem@L52706`, `rule.15.ProcedureDeclOkJudgement@L52728`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52741`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52757`, `req.15.ExportedProcedureForeignCallableObligations@L52773`, `def.15.MainEntryPointDefinitions@L52786`, `rule.15.Main-Ok@L52807`, `rule.15.Main-Bypass-NonExecutable@L52823`
- `rule.15.Main-Multiple@L52839`, `rule.15.Main-Generic-Err@L52855`, `rule.15.Main-Signature-Err@L52871`, `rule.15.Main-Missing@L52887`, `def.15.MainDiagRefs@L52903`, `def.15.FuncValDefined@L52918`, `def.15.BindParams@L52931`, `def.15.ArgumentPassingJudgements@L52944`
- `def.15.CallJudgements@L52958`, `def.15.CallTargets@L52971`, `def.15.BuiltinProcedureParams@L52987`, `def.15.SynthParams@L53001`, `def.15.CalleeProc@L53014`, `def.15.CallParams@L53028`, `def.15.ReturnOut@L53044`, `rule.15.EvalArgsSigma-Empty@L53063`
- `rule.15.EvalArgsSigma-Cons-Move@L53078`, `rule.15.EvalArgsSigma-Cons-Ref@L53094`, `rule.15.EvalArgsSigma-Ctrl-Move@L53110`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53126`, `rule.15.ApplyRegionProc-NewScoped@L53142`, `rule.15.ApplyRegionProc-Alloc@L53158`, `rule.15.ApplyRegionProc-Reset@L53174`, `rule.15.ApplyRegionProc-Freeze@L53190`
- `rule.15.ApplyRegionProc-Thaw@L53206`, `rule.15.ApplyRegionProc-Free@L53222`, `rule.15.ApplyCancelProc-New@L53238`, `rule.15.ApplyProcSigma@L53254`, `rule.15.EvalSigma-Call-Proc@L53270`, `rule.15.CG-Item-Procedure@L53288`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53304`, `diag.15.ProcedureDeclarations@L53319`
- `grammar.15.MethodsAndReceivers@L53336`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53353`, `rule.15.Parse-MethodDefAfterVis@L53368`, `rule.15.Parse-Override-Yes@L53384`, `rule.15.Parse-Override-No@L53400`, `rule.15.Parse-MethodSignature@L53416`, `rule.15.Parse-StateMethodSignature-Receiver@L53432`, `rule.15.Parse-MethodParams-None@L53448`
- `rule.15.Parse-MethodParams-Comma@L53464`, `rule.15.Parse-Receiver-Short-Const@L53480`, `rule.15.Parse-Receiver-Short-Unique@L53496`, `rule.15.Parse-Receiver-Short-Shared@L53512`, `rule.15.Parse-Receiver-Explicit@L53528`, `def.15.MethodDeclAst@L53546`, `def.15.ReceiverAst@L53559`, `def.15.RecordFieldsMethodsAndSelf@L53574`
- `def.15.SelfType@L53589`, `def.15.RecvType@L53602`, `def.15.RecvMode@L53618`, `def.15.RecvPerm@L53632`, `def.15.MethodSignaturesAndParams@L53647`, `rule.15.Recv-Explicit@L53666`, `rule.15.Record-Method-RecvSelf-Err@L53682`, `rule.15.Recv-Const@L53698`
- `rule.15.Recv-Unique@L53713`, `rule.15.Recv-Shared@L53728`, `rule.15.WF-Record-Method@L53743`, `rule.15.T-Record-Method-Body@L53759`, `rule.15.WF-Record-Methods@L53775`, `rule.15.Record-Method-Dup@L53791`, `def.15.ArgsOkJudg@L53807`, `def.15.RecvBaseType@L53820`
- `rule.15.Args-Empty@L53833`, `rule.15.Args-Cons@L53848`, `rule.15.Args-Cons-Ref@L53864`, `def.15.RecvArgOk@L53880`, `rule.15.T-Record-MethodCall@L53893`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53909`, `def.15.RecvArgMode@L53924`, `def.15.MethodOf@L53938`
- `def.15.RecvBase@L53953`, `def.15.RecvParams@L53966`, `rule.15.EvalRecvSigma-Move@L53981`, `rule.15.EvalRecvSigma-Ref-Dyn@L53997`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L54013`, `rule.15.EvalRecvSigma-Ref@L54029`, `rule.15.EvalRecvSigma-Ctrl-Move@L54045`, `rule.15.EvalRecvSigma-Ctrl-Ref@L54061`
- `def.15.BindMethodParams@L54077`, `rule.15.ApplyMethodSigma-Prim@L54090`, `rule.15.ApplyMethodSigma@L54106`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54124`, `rule.15.Mangle-Record-Method@L54137`, `rule.15.Mangle-Class-Method@L54153`, `rule.15.Mangle-State-Method@L54169`, `diag.15.MethodsAndReceivers@L54187`
- `req.15.OverloadingNoAdditionalSyntax@L54204`, `req.15.OverloadResolutionNotParserConcern@L54219`, `def.15.ClassDefaults@L54234`, `def.15.LookupMethod@L54247`, `rule.15.LookupMethod-NotFound@L54264`, `rule.15.LookupMethod-Ambig@L54280`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54296`, `req.15.FreeCallOverloadResolutionAlgorithm@L54309`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54331`, `req.15.NoRuntimeOverloadSearch@L54346`, `req.15.OverloadResolutionCompleteBeforeLowering@L54361`, `diag-table.15.Overloading@L54376`, `diag.15.MethodLookupDiagnostics@L54393`, `grammar.15.ContractClauses@L54410`, `req.15.ForeignContractStartDisambiguatesContracts@L54432`, `rule.15.Parse-ContractClauseOpt-None@L54445`
- `rule.15.Parse-ContractClauseOpt-Yes@L54461`, `rule.15.Parse-ContractBody-PostOnly@L54477`, `rule.15.Parse-ContractBody-PrePost@L54493`, `rule.15.Parse-ContractBody-PreOnly@L54509`, `def.15.ContractClauseAst@L54527`, `def.15.ContractOpt@L54540`, `rule.15.WF-Contract@L54555`, `def.15.ContractPurityJudgementIntro@L54572`
- `rule.15.Pure-Literal@L54585`, `rule.15.Pure-Ident@L54601`, `rule.15.Pure-Field@L54617`, `rule.15.Pure-Tuple-Access@L54633`, `rule.15.Pure-Index@L54649`, `rule.15.Pure-Unary@L54665`, `rule.15.Pure-Binary@L54681`, `def.15.PureOps@L54697`
- `rule.15.Pure-Cast@L54710`, `rule.15.Pure-If@L54726`, `rule.15.Pure-If-Is@L54742`, `rule.15.Pure-If-Is-No-Else@L54758`, `rule.15.Pure-If-Case@L54774`, `rule.15.Pure-If-Case-No-Else@L54790`, `rule.15.Pure-Block@L54806`, `rule.15.Pure-Tuple@L54822`
- `rule.15.Pure-Array@L54838`, `rule.15.Pure-Record@L54854`, `rule.15.Pure-Call-Builtin@L54870`, `rule.15.Pure-Call-Procedure@L54886`, `rule.15.Pure-Method-Const@L54902`, `rule.15.Pure-Comptime@L54918`, `def.15.ContractPurityHelperPredicates@L54934`, `req.15.ContractNeverPureForms@L54954`
- `def.15.PreconditionEvaluationContext@L54967`, `def.15.PostconditionEvaluationContext@L54980`, `req.15.ContractClausesNoIndependentRuntimeEffect@L54995`, `req.15.ContractClauseLoweringViaVerificationResults@L55010`, `diag.15.ContractClauses@L55025`, `req.15.PreconditionSyntaxDefinition@L55042`, `req.15.PreconditionsParsedByContractBody@L55057`, `def.15.PreconditionOf@L55072`
- `def.15.PreconditionProofContext@L55089`, `rule.15.Pre-Satisfied@L55105`, `def.15.PreconditionElisionRules@L55121`, `req.15.CallerResponsibleForPrecondition@L55141`, `req.15.PreconditionRuntimeEvaluationOrder@L55156`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55171`, `diag.15.Preconditions@L55186`, `grammar.15.Postconditions@L55203`
- `rule.15.Parse-Contract-Result@L55221`, `rule.15.Parse-Contract-Entry@L55237`, `def.15.ContractIntrinsicAst@L55255`, `def.15.PostconditionOf@L55268`, `def.15.PostconditionProofContext@L55285`, `rule.15.Post-Valid@L55300`, `def.15.PostconditionElisionRules@L55316`, `req.15.ContractResultProperties@L55336`
- `rule.15.Result-Union-Type@L55353`, `rule.15.Result-Is-Predicate@L55369`, `rule.15.Result-Narrowing@L55385`, `rule.15.Propagate-Postcondition@L55401`, `rule.15.Result-Modal@L55417`, `rule.15.Result-Generic@L55433`, `rule.15.Result-Generic-Constraint@L55449`, `req.15.ContractEntryConstraints@L55465`
- `rule.15.Entry-Type@L55483`, `req.15.PostconditionResultRuntimeBinding@L55501`, `req.15.ContractEntryRuntimeCapture@L55514`, `def.15.EntryCaptureTiming@L55531`, `rule.15.EntryCapturePhase@L55551`, `def.15.EntryCaptureValue@L55568`, `req.15.PostconditionLoweringRepresentation@L55583`, `diag.15.Postconditions@L55598`
- `grammar.15.Invariants@L55615`, `rule.15.Parse-InvariantOpt-None@L55633`, `rule.15.Parse-InvariantOpt-Yes@L55649`, `rule.15.ParseLoopInvariantOpt@L55665`, `def.15.InvariantAst@L55680`, `def.15.TypeInvariantAstExtensions@L55694`, `def.15.LoopInvariantAstPreservation@L55709`, `def.15.TypeInvariantContext@L55724`
- `def.15.TypeInvariantEnforcementPoints@L55741`, `req.15.TypeInvariantsForbidPublicMutableFields@L55758`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55771`, `def.15.LoopInvariantEnforcementPoints@L55784`, `req.15.LoopInvariantExitFact@L55801`, `req.15.InvariantVerificationModeRules@L55814`, `req.15.InvariantRuntimeChecks@L55829`, `req.15.InvariantLoweringViaVerificationLogic@L55844`
- `diag.15.Invariants@L55859`, `req.15.VerificationLogicNoSurfaceSyntax@L55876`, `req.15.VerificationLogicNotParserOwned@L55891`, `def.15.ContractKind@L55906`, `def.15.VerificationFact@L55919`, `def.15.CheckState@L55932`, `def.15.ContractCheck@L55945`, `def.15.DynamicScopeAndContext@L55960`
- `rule.15.Contract-Static-OK@L55981`, `rule.15.Contract-Static-Fail@L55997`, `rule.15.Contract-Dynamic-Elide@L56013`, `rule.15.Contract-Dynamic-Check@L56029`, `req.15.MandatoryProofTechniques@L56045`, `def.15.ProofContextAt@L56065`, `def.15.DecidablePredicates@L56086`, `rule.15.Ent-True@L56106`
- `rule.15.Ent-Fact@L56122`, `rule.15.Ent-And@L56138`, `rule.15.Ent-Or-L@L56154`, `rule.15.Ent-Or-R@L56170`, `rule.15.Ent-Linear@L56186`, `def.15.LinearIntegerEntailment@L56202`, `req.15.LinearEntailmentSoundAndComplete@L56225`, `def.15.StaticProofAt@L56238`
- `def.15.NegFact@L56251`, `req.15.VerificationFactsNoRuntimeRepresentation@L56273`, `rule.15.Fact-Dominate@L56290`, `req.15.FactGeneration@L56306`, `req.15.TypeNarrowingFromFacts@L56329`, `def.15.ContractEnvironments@L56344`, `rule.15.Check-True@L56361`, `rule.15.Check-False@L56377`
- `rule.15.Check-Panic@L56393`, `rule.15.Check-Ok@L56409`, `rule.15.Check-Fail@L56425`, `req.15.DynamicChecksInjectFacts@L56441`, `def.15.RuntimeCheckInsertionPointsIntro@L56456`, `rule.15.Insert-Precondition-Check@L56469`, `rule.15.Insert-Postcondition-Check@L56485`, `rule.15.Insert-TypeInv-Construction-Check@L56501`
- `rule.15.Insert-TypeInv-PreCall-Check@L56517`, `rule.15.Insert-TypeInv-PostCall-Check@L56533`, `rule.15.Insert-LoopInv-Init-Check@L56549`, `rule.15.Insert-LoopInv-Maintenance-Check@L56565`, `rule.15.Insert-Refinement-Check@L56581`, `diag.15.VerificationLogic@L56599`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56616`, `req.15.BehavioralSubtypingNotParserOwned@L56631`
- `def.15.BehavioralSubtypingRelationship@L56646`, `req.15.BehavioralSubtypingLiskovRequirement@L56661`, `req.15.BehavioralSubtypingPreconditionRule@L56674`, `req.15.BehavioralSubtypingPostconditionRule@L56690`, `req.15.BehavioralSubtypingVerificationStrategy@L56706`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56722`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56737`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56752`
- `diag.15.BehavioralSubtyping@L56767`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56782`, `diag-table.15.ProcedureContractEntryDiagnostics@L56795`
- `grammar.15.ProcedureDeclarations@L52337`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52355`, `rule.15.Parse-Procedure@L52370`, `rule.15.Parse-Signature@L52386`, `rule.15.Parse-ParamList-Empty@L52402`, `rule.15.Parse-ParamList-Cons@L52418`, `rule.15.Parse-Param@L52434`, `rule.15.Parse-ParamMode-None@L52450`
- `rule.15.Parse-ParamMode-Move@L52466`, `rule.15.Parse-ParamTail-End@L52482`, `rule.15.Parse-ParamTail-TrailingComma@L52498`, `rule.15.Parse-ParamTail-Comma@L52514`, `rule.15.Parse-ReturnOpt-None@L52530`, `rule.15.Parse-ReturnOpt-Arrow@L52546`, `def.15.ProcedureDeclAst@L52564`, `def.15.ParamAst@L52577`
- `def.15.ParamNamesAndBinds@L52590`, `def.15.ProcReturn@L52604`, `def.15.BodyReturnType@L52619`, `def.15.ExplicitReturn@L52634`, `def.15.ReturnAnnOk@L52649`, `rule.15.WF-ProcedureDecl@L52662`, `def.15.DeclTyping@L52678`, `def.15.ProvBindCheck@L52693`
- `def.15.DeclTypingItem@L52706`, `rule.15.ProcedureDeclOkJudgement@L52728`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52741`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52757`, `req.15.ExportedProcedureForeignCallableObligations@L52773`, `def.15.MainEntryPointDefinitions@L52786`, `rule.15.Main-Ok@L52807`, `rule.15.Main-Bypass-NonExecutable@L52823`
- `rule.15.Main-Multiple@L52839`, `rule.15.Main-Generic-Err@L52855`, `rule.15.Main-Signature-Err@L52871`, `rule.15.Main-Missing@L52887`, `def.15.MainDiagRefs@L52903`, `def.15.FuncValDefined@L52918`, `def.15.BindParams@L52931`, `def.15.ArgumentPassingJudgements@L52944`
- `def.15.CallJudgements@L52958`, `def.15.CallTargets@L52971`, `def.15.BuiltinProcedureParams@L52987`, `def.15.SynthParams@L53001`, `def.15.CalleeProc@L53014`, `def.15.CallParams@L53028`, `def.15.ReturnOut@L53044`, `rule.15.EvalArgsSigma-Empty@L53063`
- `rule.15.EvalArgsSigma-Cons-Move@L53078`, `rule.15.EvalArgsSigma-Cons-Ref@L53094`, `rule.15.EvalArgsSigma-Ctrl-Move@L53110`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53126`, `rule.15.ApplyRegionProc-NewScoped@L53142`, `rule.15.ApplyRegionProc-Alloc@L53158`, `rule.15.ApplyRegionProc-Reset@L53174`, `rule.15.ApplyRegionProc-Freeze@L53190`
- `rule.15.ApplyRegionProc-Thaw@L53206`, `rule.15.ApplyRegionProc-Free@L53222`, `rule.15.ApplyCancelProc-New@L53238`, `rule.15.ApplyProcSigma@L53254`, `rule.15.EvalSigma-Call-Proc@L53270`, `rule.15.CG-Item-Procedure@L53288`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53304`, `diag.15.ProcedureDeclarations@L53319`
- `grammar.15.MethodsAndReceivers@L53336`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53353`, `rule.15.Parse-MethodDefAfterVis@L53368`, `rule.15.Parse-Override-Yes@L53384`, `rule.15.Parse-Override-No@L53400`, `rule.15.Parse-MethodSignature@L53416`, `rule.15.Parse-StateMethodSignature-Receiver@L53432`, `rule.15.Parse-MethodParams-None@L53448`
- `rule.15.Parse-MethodParams-Comma@L53464`, `rule.15.Parse-Receiver-Short-Const@L53480`, `rule.15.Parse-Receiver-Short-Unique@L53496`, `rule.15.Parse-Receiver-Short-Shared@L53512`, `rule.15.Parse-Receiver-Explicit@L53528`, `def.15.MethodDeclAst@L53546`, `def.15.ReceiverAst@L53559`, `def.15.RecordFieldsMethodsAndSelf@L53574`
- `def.15.SelfType@L53589`, `def.15.RecvType@L53602`, `def.15.RecvMode@L53618`, `def.15.RecvPerm@L53632`, `def.15.MethodSignaturesAndParams@L53647`, `rule.15.Recv-Explicit@L53666`, `rule.15.Record-Method-RecvSelf-Err@L53682`, `rule.15.Recv-Const@L53698`
- `rule.15.Recv-Unique@L53713`, `rule.15.Recv-Shared@L53728`, `rule.15.WF-Record-Method@L53743`, `rule.15.T-Record-Method-Body@L53759`, `rule.15.WF-Record-Methods@L53775`, `rule.15.Record-Method-Dup@L53791`, `def.15.ArgsOkJudg@L53807`, `def.15.RecvBaseType@L53820`
- `rule.15.Args-Empty@L53833`, `rule.15.Args-Cons@L53848`, `rule.15.Args-Cons-Ref@L53864`, `def.15.RecvArgOk@L53880`, `rule.15.T-Record-MethodCall@L53893`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53909`, `def.15.RecvArgMode@L53924`, `def.15.MethodOf@L53938`
- `def.15.RecvBase@L53953`, `def.15.RecvParams@L53966`, `rule.15.EvalRecvSigma-Move@L53981`, `rule.15.EvalRecvSigma-Ref-Dyn@L53997`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L54013`, `rule.15.EvalRecvSigma-Ref@L54029`, `rule.15.EvalRecvSigma-Ctrl-Move@L54045`, `rule.15.EvalRecvSigma-Ctrl-Ref@L54061`
- `def.15.BindMethodParams@L54077`, `rule.15.ApplyMethodSigma-Prim@L54090`, `rule.15.ApplyMethodSigma@L54106`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54124`, `rule.15.Mangle-Record-Method@L54137`, `rule.15.Mangle-Class-Method@L54153`, `rule.15.Mangle-State-Method@L54169`, `diag.15.MethodsAndReceivers@L54187`
- `req.15.OverloadingNoAdditionalSyntax@L54204`, `req.15.OverloadResolutionNotParserConcern@L54219`, `def.15.ClassDefaults@L54234`, `def.15.LookupMethod@L54247`, `rule.15.LookupMethod-NotFound@L54264`, `rule.15.LookupMethod-Ambig@L54280`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54296`, `req.15.FreeCallOverloadResolutionAlgorithm@L54309`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54331`, `req.15.NoRuntimeOverloadSearch@L54346`, `req.15.OverloadResolutionCompleteBeforeLowering@L54361`, `diag-table.15.Overloading@L54376`, `diag.15.MethodLookupDiagnostics@L54393`, `grammar.15.ContractClauses@L54410`, `req.15.ForeignContractStartDisambiguatesContracts@L54432`, `rule.15.Parse-ContractClauseOpt-None@L54445`
- `rule.15.Parse-ContractClauseOpt-Yes@L54461`, `rule.15.Parse-ContractBody-PostOnly@L54477`, `rule.15.Parse-ContractBody-PrePost@L54493`, `rule.15.Parse-ContractBody-PreOnly@L54509`, `def.15.ContractClauseAst@L54527`, `def.15.ContractOpt@L54540`, `rule.15.WF-Contract@L54555`, `def.15.ContractPurityJudgementIntro@L54572`
- `rule.15.Pure-Literal@L54585`, `rule.15.Pure-Ident@L54601`, `rule.15.Pure-Field@L54617`, `rule.15.Pure-Tuple-Access@L54633`, `rule.15.Pure-Index@L54649`, `rule.15.Pure-Unary@L54665`, `rule.15.Pure-Binary@L54681`, `def.15.PureOps@L54697`
- `rule.15.Pure-Cast@L54710`, `rule.15.Pure-If@L54726`, `rule.15.Pure-If-Is@L54742`, `rule.15.Pure-If-Is-No-Else@L54758`, `rule.15.Pure-If-Case@L54774`, `rule.15.Pure-If-Case-No-Else@L54790`, `rule.15.Pure-Block@L54806`, `rule.15.Pure-Tuple@L54822`
- `rule.15.Pure-Array@L54838`, `rule.15.Pure-Record@L54854`, `rule.15.Pure-Call-Builtin@L54870`, `rule.15.Pure-Call-Procedure@L54886`, `rule.15.Pure-Method-Const@L54902`, `rule.15.Pure-Comptime@L54918`, `def.15.ContractPurityHelperPredicates@L54934`, `req.15.ContractNeverPureForms@L54954`
- `def.15.PreconditionEvaluationContext@L54967`, `def.15.PostconditionEvaluationContext@L54980`, `req.15.ContractClausesNoIndependentRuntimeEffect@L54995`, `req.15.ContractClauseLoweringViaVerificationResults@L55010`, `diag.15.ContractClauses@L55025`, `req.15.PreconditionSyntaxDefinition@L55042`, `req.15.PreconditionsParsedByContractBody@L55057`, `def.15.PreconditionOf@L55072`
- `def.15.PreconditionProofContext@L55089`, `rule.15.Pre-Satisfied@L55105`, `def.15.PreconditionElisionRules@L55121`, `req.15.CallerResponsibleForPrecondition@L55141`, `req.15.PreconditionRuntimeEvaluationOrder@L55156`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55171`, `diag.15.Preconditions@L55186`, `grammar.15.Postconditions@L55203`
- `rule.15.Parse-Contract-Result@L55221`, `rule.15.Parse-Contract-Entry@L55237`, `def.15.ContractIntrinsicAst@L55255`, `def.15.PostconditionOf@L55268`, `def.15.PostconditionProofContext@L55285`, `rule.15.Post-Valid@L55300`, `def.15.PostconditionElisionRules@L55316`, `req.15.ContractResultProperties@L55336`
- `rule.15.Result-Union-Type@L55353`, `rule.15.Result-Is-Predicate@L55369`, `rule.15.Result-Narrowing@L55385`, `rule.15.Propagate-Postcondition@L55401`, `rule.15.Result-Modal@L55417`, `rule.15.Result-Generic@L55433`, `rule.15.Result-Generic-Constraint@L55449`, `req.15.ContractEntryConstraints@L55465`
- `rule.15.Entry-Type@L55483`, `req.15.PostconditionResultRuntimeBinding@L55501`, `req.15.ContractEntryRuntimeCapture@L55514`, `def.15.EntryCaptureTiming@L55531`, `rule.15.EntryCapturePhase@L55551`, `def.15.EntryCaptureValue@L55568`, `req.15.PostconditionLoweringRepresentation@L55583`, `diag.15.Postconditions@L55598`
- `grammar.15.Invariants@L55615`, `rule.15.Parse-InvariantOpt-None@L55633`, `rule.15.Parse-InvariantOpt-Yes@L55649`, `rule.15.ParseLoopInvariantOpt@L55665`, `def.15.InvariantAst@L55680`, `def.15.TypeInvariantAstExtensions@L55694`, `def.15.LoopInvariantAstPreservation@L55709`, `def.15.TypeInvariantContext@L55724`
- `def.15.TypeInvariantEnforcementPoints@L55741`, `req.15.TypeInvariantsForbidPublicMutableFields@L55758`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55771`, `def.15.LoopInvariantEnforcementPoints@L55784`, `req.15.LoopInvariantExitFact@L55801`, `req.15.InvariantVerificationModeRules@L55814`, `req.15.InvariantRuntimeChecks@L55829`, `req.15.InvariantLoweringViaVerificationLogic@L55844`
- `diag.15.Invariants@L55859`, `req.15.VerificationLogicNoSurfaceSyntax@L55876`, `req.15.VerificationLogicNotParserOwned@L55891`, `def.15.ContractKind@L55906`, `def.15.VerificationFact@L55919`, `def.15.CheckState@L55932`, `def.15.ContractCheck@L55945`, `def.15.DynamicScopeAndContext@L55960`
- `rule.15.Contract-Static-OK@L55981`, `rule.15.Contract-Static-Fail@L55997`, `rule.15.Contract-Dynamic-Elide@L56013`, `rule.15.Contract-Dynamic-Check@L56029`, `req.15.MandatoryProofTechniques@L56045`, `def.15.ProofContextAt@L56065`, `def.15.DecidablePredicates@L56086`, `rule.15.Ent-True@L56106`
- `rule.15.Ent-Fact@L56122`, `rule.15.Ent-And@L56138`, `rule.15.Ent-Or-L@L56154`, `rule.15.Ent-Or-R@L56170`, `rule.15.Ent-Linear@L56186`, `def.15.LinearIntegerEntailment@L56202`, `req.15.LinearEntailmentSoundAndComplete@L56225`, `def.15.StaticProofAt@L56238`
- `def.15.NegFact@L56251`, `req.15.VerificationFactsNoRuntimeRepresentation@L56273`, `rule.15.Fact-Dominate@L56290`, `req.15.FactGeneration@L56306`, `req.15.TypeNarrowingFromFacts@L56329`, `def.15.ContractEnvironments@L56344`, `rule.15.Check-True@L56361`, `rule.15.Check-False@L56377`
- `rule.15.Check-Panic@L56393`, `rule.15.Check-Ok@L56409`, `rule.15.Check-Fail@L56425`, `req.15.DynamicChecksInjectFacts@L56441`, `def.15.RuntimeCheckInsertionPointsIntro@L56456`, `rule.15.Insert-Precondition-Check@L56469`, `rule.15.Insert-Postcondition-Check@L56485`, `rule.15.Insert-TypeInv-Construction-Check@L56501`
- `rule.15.Insert-TypeInv-PreCall-Check@L56517`, `rule.15.Insert-TypeInv-PostCall-Check@L56533`, `rule.15.Insert-LoopInv-Init-Check@L56549`, `rule.15.Insert-LoopInv-Maintenance-Check@L56565`, `rule.15.Insert-Refinement-Check@L56581`, `diag.15.VerificationLogic@L56599`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56616`, `req.15.BehavioralSubtypingNotParserOwned@L56631`
- `def.15.BehavioralSubtypingRelationship@L56646`, `req.15.BehavioralSubtypingLiskovRequirement@L56661`, `req.15.BehavioralSubtypingPreconditionRule@L56674`, `req.15.BehavioralSubtypingPostconditionRule@L56690`, `req.15.BehavioralSubtypingVerificationStrategy@L56706`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56722`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56737`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56752`
- `diag.15.BehavioralSubtyping@L56767`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56782`, `diag-table.15.ProcedureContractEntryDiagnostics@L56795`

### Language Constructs, Dynamic Semantics, And Feature Semantics

#### `spec.expressions`

Count: 478 total; 475 required; 0 recommended; 0 informative. Ledger line span: L56475-L64152.

- `grammar.16.LiteralAndNameExpressions@L56840`, `req.16.QualifiedApplicationOwnership@L56858`, `rule.16.Parse-Literal-Expr@L56873`, `rule.16.Parse-Null-Ptr@L56889`, `rule.16.Parse-Identifier-Expr@L56905`, `rule.16.Parse-Qualified-Name@L56921`, `def.16.LiteralKindAndToken@L56939`, `def.16.LiteralNameExprAst@L56953`
- `def.16.QualifiedNameResolution@L56967`, `def.16.ValuePathType@L56985`, `def.16.NumericLiteralTypeSets@L57008`, `def.16.NumericLiteralParsingHelpers@L57025`, `rule.16.T-Int-Literal-Suffix@L57052`, `rule.16.T-Int-Literal-Default@L57068`, `rule.16.T-Float-Literal-Explicit@L57084`, `rule.16.T-Float-Literal-Infer@L57100`
- `rule.16.T-Bool-Literal@L57116`, `rule.16.T-Char-Literal@L57132`, `rule.16.T-String-Literal@L57148`, `rule.16.Syn-Literal@L57164`, `def.16.NullLiteralExpected@L57180`, `rule.16.Chk-Int-Literal@L57193`, `rule.16.Chk-Float-Literal-Explicit@L57209`, `rule.16.Chk-Float-Literal-Infer@L57225`
- `rule.16.Chk-Null-Literal@L57241`, `def.16.PtrNullExpected@L57257`, `rule.16.Chk-Null-Ptr@L57270`, `rule.16.Syn-PtrNull-Err@L57286`, `rule.16.Chk-PtrNull-Err@L57302`, `rule.16.T-Ident@L57318`, `rule.16.T-Path-Value@L57334`, `rule.16.Expr-Unresolved-Err@L57350`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57366`, `def.16.EvaluationJudgements@L57381`, `def.16.LiteralRuntimeValues@L57396`, `rule.16.EvalSigma-Literal@L57417`, `rule.16.EvalSigma-PtrNull@L57433`, `rule.16.EvalSigma-Ident@L57448`, `rule.16.EvalSigma-Path@L57464`, `rule.16.EvalSigma-ErrorExpr@L57480`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57495`, `rule.16.Lower-Expr-Literal@L57510`, `rule.16.Lower-Expr-PtrNull@L57526`, `rule.16.Lower-Expr-Ident-Local@L57541`, `rule.16.Lower-Expr-Ident-Path@L57557`, `rule.16.Lower-Expr-Path@L57573`, `rule.16.Lower-Expr-Error@L57588`, `diag.16.LiteralAndNameExpressions@L57605`
- `grammar.16.AccessAndPlaceExpressions@L57622`, `req.16.AccessPostfixOwnership@L57638`, `rule.16.Postfix-Field@L57653`, `rule.16.Postfix-TupleIndex@L57669`, `rule.16.Postfix-Index@L57685`, `def.16.IsPlace@L57701`, `rule.16.Parse-Place-Deref@L57714`, `rule.16.Parse-Place-Postfix@L57730`
- `rule.16.Parse-Place-Err@L57746`, `def.16.AccessPlaceAst@L57764`, `def.16.PlaceForms0@L57777`, `def.16.FieldVisibility@L57790`, `def.16.IndexClassification@L57804`, `rule.16.T-Field-Record@L57821`, `rule.16.T-Field-Record-Perm@L57837`, `rule.16.P-Field-Record@L57853`
- `rule.16.P-Field-Record-Perm@L57869`, `rule.16.T-Tuple-Index@L57885`, `rule.16.T-Tuple-Index-Perm@L57901`, `rule.16.P-Tuple-Index@L57917`, `rule.16.P-Tuple-Index-Perm@L57933`, `rule.16.T-Index-Array@L57949`, `rule.16.T-Index-Array-Dynamic@L57965`, `rule.16.T-Index-Array-Perm@L57981`
- `rule.16.T-Index-Array-Perm-Dynamic@L57997`, `rule.16.T-Index-Slice@L58013`, `rule.16.T-Index-Slice-Perm@L58029`, `rule.16.T-Slice-From-Array@L58045`, `rule.16.T-Slice-From-Array-Perm@L58061`, `rule.16.T-Slice-From-Slice@L58077`, `rule.16.T-Slice-From-Slice-Perm@L58093`, `rule.16.PlaceIndexAndSliceCounterparts@L58109`
- `rule.16.Coerce-Array-Slice@L58122`, `rule.16.Union-DirectAccess-Err@L58138`, `rule.16.ValueUse-NonBitcopyPlace@L58154`, `rule.16.EvalSigma-FieldAccess@L58172`, `rule.16.EvalSigma-TupleAccess@L58188`, `rule.16.EvalSigma-Index@L58204`, `rule.16.EvalSigma-Index-Range@L58220`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58236`
- `rule.16.Lower-Expr-FieldAccess@L58251`, `rule.16.Lower-Expr-TupleAccess@L58267`, `rule.16.Lower-Expr-IndexFamily@L58283`, `rule.16.Lower-Place-Ident@L58296`, `rule.16.Lower-Place-Field@L58311`, `rule.16.Lower-Place-Tuple@L58327`, `rule.16.Lower-Place-Index@L58343`, `rule.16.Lower-Place-Deref@L58359`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58375`, `diag.16.AccessAndPlaceExpressions@L58390`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58403`, `grammar.16.CallExpressions@L58420`, `req.16.QualifiedApplyParenPreResolution@L58439`, `rule.16.Postfix-Call@L58454`, `rule.16.Postfix-Call-TypeArgs@L58470`, `rule.16.Postfix-MethodCall@L58486`
- `rule.16.Parse-Qualified-Apply-Paren@L58502`, `rule.16.ArgumentListParsingFamily@L58518`, `def.16.ArgAst@L58533`, `def.16.CallExprAst@L58546`, `def.16.ArgAccessors@L58559`, `def.16.MovedArg@L58573`, `req.16.QualifiedParenthesizedApplicationResolution@L58588`, `def.16.CallStaticJudgementsAndArgumentTyping@L58607`
- `rule.16.ArgsT-Empty@L58635`, `rule.16.ArgsT-Cons@L58650`, `rule.16.ArgsT-Cons-Ref@L58666`, `rule.16.T-Call-Generic-Infer@L58682`, `rule.16.T-Call@L58698`, `rule.16.Call-Callee-NotFunc@L58714`, `rule.16.Call-ArgCount-Err@L58730`, `rule.16.Call-ArgType-Err@L58746`
- `rule.16.Call-Move-Missing@L58762`, `rule.16.Call-Move-Unexpected@L58778`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58794`, `rule.16.Call-Arg-NotPlace@L58810`, `rule.16.Chk-Call-Generic-Infer@L58826`, `req.16.CallTypeArgsStaticOwnership@L58842`, `req.16.MethodRecordClosureCallStaticOwnership@L58855`, `req.16.ExternProcedureCallsRequireUnsafe@L58868`
- `rule.16.EvalSigma-Call-Closure@L58883`, `rule.16.EvalSigma-Call-RegionProc@L58899`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58915`, `rule.16.EvalSigma-Call-CancelProc@L58931`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58947`, `rule.16.EvalSigma-Call-Proc@L58963`, `rule.16.EvalSigma-Call-Record@L58979`, `rule.16.EvalSigma-MethodCall@L58995`
- `req.16.CallControlPropagation@L59011`, `req.16.MethodCallControlPropagation@L59024`, `req.16.CallTypeArgsEvaluationElaboration@L59037`, `rule.16.Lower-Args-Empty@L59052`, `rule.16.Lower-Args-Cons-Move@L59067`, `rule.16.Lower-Args-Cons-Ref@L59083`, `rule.16.Lower-Expr-Call-Closure@L59099`, `rule.16.Lower-Expr-CallFamily@L59115`
- `rule.16.Lower-MethodCallFamily@L59128`, `req.16.CallTypeArgsLoweringElaboration@L59141`, `diag.16.CallExpressions@L59156`, `grammar.16.OperatorExpressions@L59173`, `req.16.OperatorPrefixSyntaxOwnership@L59204`, `rule.16.ParseRangeFamily@L59219`, `rule.16.ParseLeftChainFamily@L59232`, `rule.16.ParsePowerFamily@L59245`
- `rule.16.Parse-Unary-Prefix@L59258`, `def.16.RangeAndOperatorExprAst@L59276`, `def.16.OperatorSets@L59290`, `def.16.OperatorStaticTypes@L59308`, `rule.16.T-Range-Lift@L59323`, `rule.16.RangeTypingFamily@L59339`, `rule.16.T-Not-Bool@L59352`, `rule.16.T-Not-Int@L59368`
- `rule.16.T-Neg@L59384`, `rule.16.T-Arith@L59400`, `rule.16.T-Bitwise@L59416`, `rule.16.T-Shift@L59432`, `rule.16.T-Compare-Eq@L59448`, `rule.16.T-Compare-Ord@L59464`, `rule.16.T-Logical@L59480`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59498`
- `def.16.OperatorComparisonRuntime@L59520`, `def.16.OperatorBitShiftArithmeticRuntime@L59540`, `def.16.UnaryOperatorRuntime@L59560`, `req.16.FloatUnaryNegationTotality@L59578`, `def.16.BinaryOperatorRuntime@L59591`, `rule.16.EvalSigma-Range@L59612`, `rule.16.EvalSigma-Unary@L59628`, `rule.16.EvalSigma-Bin-And-False@L59644`
- `rule.16.EvalSigma-Bin-And-True@L59660`, `rule.16.EvalSigma-Bin-Or-True@L59676`, `rule.16.EvalSigma-Bin-Or-False@L59692`, `rule.16.EvalSigma-Binary@L59708`, `req.16.OperatorUndefinedAndNaNBehavior@L59724`, `rule.16.Lower-Expr-Unary@L59739`, `rule.16.Lower-Expr-Bin-And@L59755`, `rule.16.Lower-Expr-Bin-Or@L59771`
- `rule.16.Lower-Expr-Binary@L59787`, `rule.16.Lower-Expr-Range@L59803`, `def.16.UnaryOperatorLoweringPanicCheck@L59819`, `rule.16.Lower-UnOp-Ok@L59833`, `rule.16.Lower-UnOp-Panic@L59849`, `req.16.UnaryNegationLoweringOverflowChecks@L59865`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59878`, `diag.16.OperatorExpressions@L59893`
- `grammar.16.CastAndTransmuteExpressions@L59910`, `req.16.WidenPrefixOwnershipForCastTransmute@L59926`, `rule.16.Parse-Cast@L59941`, `rule.16.Parse-CastTail-None@L59957`, `rule.16.Parse-CastTail-As@L59973`, `rule.16.ParseTransmuteExprFamily@L59989`, `req.16.WidenParsingOwnershipForCastTransmute@L60002`, `def.16.CastTransmuteExprAst@L60017`
- `req.16.WidenAstOwnershipForCastTransmute@L60030`, `def.16.CastValidity@L60045`, `rule.16.T-Cast@L60060`, `rule.16.T-Cast-Invalid@L60076`, `rule.16.T-Transmute-SizeEq@L60092`, `rule.16.T-Transmute-AlignEq@L60108`, `rule.16.T-Transmute@L60124`, `rule.16.Transmute-Unsafe-Err@L60140`
- `def.16.ValidTransmuteTarget@L60156`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60173`, `def.16.CastDynamicContext@L60188`, `def.16.CastRuntimeConversionHelpers@L60202`, `rule.16.CastVal-Id@L60236`, `rule.16.CastVal-Int-Int-Signed@L60252`, `rule.16.CastVal-Int-Int-Unsigned@L60268`, `rule.16.CastVal-Int-Float@L60284`
- `req.16.IntToFloatLoweringPreservesSignedness@L60300`, `rule.16.CastVal-Float-Float@L60313`, `rule.16.CastVal-Float-Int@L60329`, `rule.16.CastVal-Bool-Int@L60345`, `rule.16.CastVal-Int-Bool@L60363`, `rule.16.CastVal-Char-U32@L60381`, `rule.16.CastVal-U32-Char@L60397`, `rule.16.EvalSigma-Cast@L60413`
- `rule.16.EvalSigma-Cast-Panic@L60429`, `def.16.TransmuteVal@L60445`, `rule.16.EvalSigma-Transmute@L60458`, `rule.16.EvalSigma-Transmute-Ctrl@L60474`, `req.16.WidenDynamicOwnershipForCastTransmute@L60490`, `rule.16.Lower-Expr-Cast@L60505`, `rule.16.Lower-Expr-Transmute@L60521`, `rule.16.LowerCastTransmuteFamily@L60537`
- `diag.16.CastAndTransmuteExpressions@L60552`, `grammar.16.ConstructionExpressions@L60569`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60591`, `rule.16.Parse-Tuple-Literal@L60606`, `rule.16.Parse-Array-Segment-Elem@L60622`, `rule.16.Parse-Array-Segment-Repeat@L60638`, `rule.16.Parse-Array-Segment-List-Empty@L60654`, `rule.16.Parse-Array-Segment-List-Single@L60669`
- `rule.16.Parse-Array-Segment-List-Comma@L60685`, `rule.16.Parse-Array-Literal@L60701`, `rule.16.Parse-Record-Literal-ModalState@L60717`, `rule.16.Parse-Record-Literal@L60733`, `rule.16.Parse-Qualified-Apply-Brace@L60749`, `rule.16.ConstructionListAndShorthandParsingFamily@L60765`, `def.16.FieldInitAst@L60780`, `def.16.ConstructionExprAst@L60793`
- `def.16.FieldInitNamesAndSet@L60806`, `req.16.QualifiedBraceApplicationResolution@L60820`, `req.16.QualifiedParenApplicationConstructionResolution@L60836`, `rule.16.T-Unit-Literal@L60851`, `rule.16.T-Tuple-Literal@L60866`, `def.16.ArraySegmentLength@L60882`, `rule.16.T-Array-Literal-Segments@L60896`, `def.16.RecordFieldNameSet@L60921`
- `rule.16.T-Record-Literal@L60935`, `rule.16.Record-FieldInit-Dup@L60951`, `rule.16.Record-FieldInit-Missing@L60967`, `rule.16.RecordFieldUnknownNotVisibleFamily@L60983`, `rule.16.Record-Field-NonBitcopy-Move@L60996`, `rule.16.EnumLiteralTypingFamily@L61012`, `def.16.RecordDefaultConstructionEligibility@L61025`, `rule.16.T-Record-Default@L61039`
- `rule.16.Record-Default-Init-Err@L61055`, `rule.16.EvalSigmaTupleConstructionFamily@L61073`, `rule.16.EvalSigmaArrayConstructionFamily@L61086`, `rule.16.EvalSigmaRecordConstructionFamily@L61099`, `rule.16.EvalSigmaEnumConstructionFamily@L61112`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61125`, `rule.16.Lower-Expr-Tuple@L61140`, `rule.16.Lower-Expr-Array@L61156`
- `rule.16.Lower-Expr-Record@L61172`, `rule.16.LowerEnumConstructionFamily@L61188`, `rule.16.Lower-CallIR-RecordCtor@L61201`, `diag.16.ConstructionExpressions@L61219`, `grammar.16.ControlExpressions@L61236`, `req.16.ControlExpressionOwnership@L61260`, `rule.16.Parse-If-Expr@L61276`, `rule.16.Parse-If-Is-Single@L61292`
- `rule.16.Parse-If-Is-CaseList@L61308`, `rule.16.Parse-Loop-Expr@L61324`, `rule.16.Parse-Block-Expr@L61340`, `rule.16.ControlExpressionParsingRemainderFamily@L61356`, `def.16.ControlExprAst@L61371`, `def.16.ControlAstHelpers@L61384`, `def.16.LoopTypeInference@L61398`, `req.16.BlockTypingOwnershipForControlExpressions@L61422`
- `rule.16.T-If@L61441`, `rule.16.T-If-No-Else@L61457`, `rule.16.CheckIfFamily@L61473`, `req.16.PatternTypingOwnershipForControlExpressions@L61486`, `rule.16.T-If-Is@L61502`, `rule.16.T-If-Is-No-Else@L61518`, `rule.16.IfCaseTypingFamily@L61534`, `rule.16.CheckIfIsAndIfCaseFamily@L61547`
- `req.16.LoopInvariantTypingOwnership@L61561`, `rule.16.T-Loop-Infinite@L61574`, `rule.16.T-Loop-Conditional@L61590`, `rule.16.T-Loop-Iter@L61606`, `rule.16.AsyncIteratorLoopTypingFamily@L61622`, `rule.16.EvalSigma-If-True@L61637`, `rule.16.EvalSigma-If-False-None@L61653`, `rule.16.EvalSigma-If-False-Some@L61669`
- `rule.16.EvalSigma-If-Ctrl@L61685`, `rule.16.EvalSigma-If-Is@L61701`, `rule.16.EvalSigma-If-Is-Ctrl@L61717`, `rule.16.EvalSigma-If-Cases@L61733`, `rule.16.EvalSigma-If-Cases-Ctrl@L61749`, `rule.16.EvalIfCasesFamily@L61765`, `rule.16.EvalSigma-Block@L61778`, `def.16.LoopIterableTypePredicates@L61794`
- `def.16.LoopIteratorRuntime@L61812`, `def.16.LoopIterJudgement@L61845`, `rule.16.EvalSigma-Loop-Infinite-Step@L61858`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61874`, `rule.16.EvalSigma-Loop-Infinite-Break@L61890`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61906`, `rule.16.EvalSigma-Loop-Cond-False@L61922`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61938`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61954`, `rule.16.EvalSigma-Loop-Cond-Break@L61970`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L61986`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L62002`, `rule.16.EvalSigma-Loop-Iter@L62018`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L62034`, `rule.16.LoopIter-Done@L62050`, `rule.16.LoopIter-Step-Val@L62066`
- `rule.16.LoopIter-Step-Continue@L62082`, `rule.16.LoopIter-Step-Break@L62098`, `rule.16.LoopIter-Step-Ctrl@L62114`, `rule.16.Lower-Expr-If@L62132`, `rule.16.Lower-Expr-If-Is@L62148`, `rule.16.Lower-Expr-If-Cases@L62164`, `rule.16.LowerLoopExpressionFamily@L62180`, `rule.16.Lower-Expr-Block@L62193`
- `req.16.ControlExpressionLoweringOwnership@L62209`, `diag.16.ControlExpressions@L62224`, `req.16.ControlExpressionDiagnosticOwnership@L62237`, `grammar.16.EffectfulCoreExpressions@L62255`, `req.16.RegionAliasAllocRewrite@L62275`, `rule.16.Parse-Unary-Deref@L62290`, `rule.16.Parse-Unary-AddressOf@L62306`, `rule.16.Parse-Unary-Move@L62322`
- `rule.16.Postfix-Propagate@L62338`, `rule.16.Parse-Alloc-Implicit@L62354`, `rule.16.Parse-Unsafe-Expr@L62370`, `def.16.EffectfulCoreExprAst@L62388`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62401`, `def.16.AddressOfStaticHelpers@L62420`, `rule.16.T-Unsafe-Expr@L62435`, `rule.16.Chk-Unsafe-Expr@L62451`
- `rule.16.T-AddrOf@L62467`, `rule.16.T-Deref-Ptr@L62483`, `rule.16.T-Deref-Raw@L62499`, `rule.16.DerefPlaceTypingFamily@L62515`, `rule.16.T-Move@L62528`, `rule.16.T-Alloc-Explicit@L62544`, `rule.16.T-Alloc-Implicit@L62560`, `def.16.SuccessMember@L62576`
- `rule.16.T-Propagate@L62589`, `def.16.SuccessMemberAsync@L62605`, `rule.16.T-Async-Try@L62618`, `rule.16.Async-Try-Infallible-Err@L62634`, `rule.16.EvalSigma-UnsafeBlock@L62652`, `rule.16.EvalSigma-AddressOf@L62668`, `rule.16.EvalSigma-Deref@L62684`, `rule.16.EvalSigma-Move@L62700`
- `rule.16.EvalSigma-Alloc-Implicit@L62716`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62732`, `rule.16.EvalSigma-Alloc-Explicit@L62748`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62764`, `rule.16.EvalSigma-Propagate-Success@L62780`, `rule.16.EvalSigma-Propagate-Success-Async@L62796`, `rule.16.EvalSigma-Propagate-Error@L62812`, `rule.16.EvalSigma-Propagate-Error-Async@L62828`
- `rule.16.EvalSigma-Propagate-Ctrl@L62845`, `def.16.ExprStateAndTerminalExpr@L62861`, `rule.16.StepSigma-Pure@L62876`, `rule.16.StepSigma-Alloc-Implicit@L62892`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62908`, `rule.16.StepSigma-Alloc-Explicit@L62924`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62940`, `rule.16.StepSigma-Block@L62956`
- `rule.16.StepSigma-UnsafeBlock@L62972`, `rule.16.StepSigma-Loop@L62988`, `rule.16.StepSigma-Stateful-Other@L63004`, `rule.16.Lower-Expr-UnsafeBlock@L63022`, `rule.16.Lower-Expr-Move@L63038`, `rule.16.Lower-Expr-AddressOf@L63054`, `rule.16.Lower-Expr-Deref@L63070`, `rule.16.Lower-Expr-Alloc@L63086`
- `rule.16.Lower-Expr-Propagate-Success@L63102`, `rule.16.Lower-Expr-Propagate-Return@L63118`, `req.16.EffectfulCoreLoweringMechanics@L63134`, `diag.16.EffectfulCoreExpressions@L63149`, `grammar.16.ClosureAndPipelineExpressions@L63166`, `req.16.ClosureParamTrailingComma@L63185`, `req.16.ClosureUnionParamParentheses@L63198`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63211`
- `rule.16.Parse-Pipeline@L63226`, `rule.16.Parse-PipelineTail-Stop@L63242`, `rule.16.Parse-PipelineTail-Cons@L63258`, `rule.16.Parse-Closure-Expr@L63274`, `rule.16.Parse-Closure-Expr-Empty@L63290`, `rule.16.Parse-ClosureParams-Single@L63306`, `rule.16.Parse-ClosureParams-Cons@L63322`, `rule.16.Parse-ClosureParamType-Grouped@L63338`
- `rule.16.Parse-ClosureParamType-Plain@L63354`, `rule.16.Parse-ClosureParam-MoveTyped@L63370`, `rule.16.Parse-ClosureParam-MoveUntyped@L63386`, `rule.16.Parse-ClosureParam-Typed@L63402`, `rule.16.Parse-ClosureParam-Untyped@L63418`, `rule.16.Parse-ClosureRetOpt-Some@L63434`, `rule.16.Parse-ClosureRetOpt-None@L63450`, `rule.16.Parse-ClosureBody-Block@L63466`
- `rule.16.Parse-ClosureBody-Expr@L63482`, `def.16.ClosurePipelineAstForms@L63500`, `def.16.ClosureCaptureSets@L63519`, `def.16.ClosureEscapeClassification@L63539`, `def.16.ClosureParameterAccessors@L63554`, `rule.16.T-Closure-NonCapturing@L63568`, `rule.16.T-Closure-Capturing@L63586`, `rule.16.T-Closure-Escaping@L63605`
- `rule.16.K-Closure-Escape-Type@L63625`, `rule.16.Capture-Const@L63641`, `rule.16.Capture-Shared@L63657`, `rule.16.Capture-Unique-Err@L63673`, `rule.16.T-ClosureCall@L63689`, `rule.16.Infer-Closure-Params@L63705`, `rule.16.Infer-Closure-Params-Err@L63721`, `rule.16.Infer-Closure-Return@L63737`
- `req.16.ClosureSharedDependencyInference@L63753`, `def.16.ClosureCaptureBindingAccessors@L63766`, `rule.16.B-Closure-NonCapturing@L63790`, `rule.16.B-Closure-Capturing@L63806`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63825`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63842`, `rule.16.B-Closure-RefCapture-Moved-Err@L63859`, `rule.16.T-Pipeline@L63876`
- `rule.16.T-Pipeline-NotCallable-Err@L63894`, `rule.16.T-Pipeline-TypeMismatch-Err@L63911`, `rule.16.T-Pipeline-ArgCount-Err@L63929`, `rule.16.B-Pipeline@L63946`, `req.16.ClosureParamInferenceFailure@L63962`, `req.16.ClosureSharedDependencyInferenceRestated@L63975`, `def.16.ClosureEnvironmentRuntimeModel@L63990`, `rule.16.EvalSigma-Closure-NonCapturing@L64026`
- `rule.16.EvalSigma-Closure-Capturing@L64042`, `def.16.MarkMoved@L64060`, `rule.16.EvalSigma-ClosureCall@L64074`, `def.16.ClosureCallRuntimeHelpers@L64092`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64114`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64130`, `req.16.ClosureCallResolvedInternalFormRuntime@L64147`, `req.16.PipelineDesugaring@L64160`
- `rule.16.EvalSigma-Pipeline-Func@L64173`, `rule.16.EvalSigma-Pipeline-Closure@L64190`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64207`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64223`, `def.16.ClosureLoweringCaptureTypes@L64241`, `rule.16.Layout-ClosureEnv@L64257`, `rule.16.Layout-ClosureEnv-Empty@L64273`, `rule.16.Lower-Expr-Closure-NonCapturing@L64289`
- `rule.16.Lower-Expr-Closure-Capturing@L64305`, `def.16.LowerCaptureEnv@L64323`, `def.16.CapturedIdentifierLoweringHelpers@L64343`, `rule.16.Lower-CapturedIdent-Ref@L64358`, `req.16.LowerCapturedIdentRefTemporaries@L64375`, `rule.16.Lower-CapturedIdent-Move@L64388`, `def.16.ClosureEnvParam@L64405`, `def.16.ClosureCodeSig@L64418`
- `rule.16.Lower-Closure-Call@L64435`, `req.16.LowerClosureCallResolvedInternalForm@L64453`, `rule.16.Lower-Expr-Pipeline@L64466`, `def.16.LowerPipelineCallablePredicates@L64484`, `diag.16.ClosureAndPipelineExpressions@L64502`, `diag.16.ExpressionDiagnosticsSupplement@L64517`
- `grammar.16.LiteralAndNameExpressions@L56840`, `req.16.QualifiedApplicationOwnership@L56858`, `rule.16.Parse-Literal-Expr@L56873`, `rule.16.Parse-Null-Ptr@L56889`, `rule.16.Parse-Identifier-Expr@L56905`, `rule.16.Parse-Qualified-Name@L56921`, `def.16.LiteralKindAndToken@L56939`, `def.16.LiteralNameExprAst@L56953`
- `def.16.QualifiedNameResolution@L56967`, `def.16.ValuePathType@L56985`, `def.16.NumericLiteralTypeSets@L57008`, `def.16.NumericLiteralParsingHelpers@L57025`, `rule.16.T-Int-Literal-Suffix@L57052`, `rule.16.T-Int-Literal-Default@L57068`, `rule.16.T-Float-Literal-Explicit@L57084`, `rule.16.T-Float-Literal-Infer@L57100`
- `rule.16.T-Bool-Literal@L57116`, `rule.16.T-Char-Literal@L57132`, `rule.16.T-String-Literal@L57148`, `rule.16.Syn-Literal@L57164`, `def.16.NullLiteralExpected@L57180`, `rule.16.Chk-Int-Literal@L57193`, `rule.16.Chk-Float-Literal-Explicit@L57209`, `rule.16.Chk-Float-Literal-Infer@L57225`
- `rule.16.Chk-Null-Literal@L57241`, `def.16.PtrNullExpected@L57257`, `rule.16.Chk-Null-Ptr@L57270`, `rule.16.Syn-PtrNull-Err@L57286`, `rule.16.Chk-PtrNull-Err@L57302`, `rule.16.T-Ident@L57318`, `rule.16.T-Path-Value@L57334`, `rule.16.Expr-Unresolved-Err@L57350`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57366`, `def.16.EvaluationJudgements@L57381`, `def.16.LiteralRuntimeValues@L57396`, `rule.16.EvalSigma-Literal@L57417`, `rule.16.EvalSigma-PtrNull@L57433`, `rule.16.EvalSigma-Ident@L57448`, `rule.16.EvalSigma-Path@L57464`, `rule.16.EvalSigma-ErrorExpr@L57480`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57495`, `rule.16.Lower-Expr-Literal@L57510`, `rule.16.Lower-Expr-PtrNull@L57526`, `rule.16.Lower-Expr-Ident-Local@L57541`, `rule.16.Lower-Expr-Ident-Path@L57557`, `rule.16.Lower-Expr-Path@L57573`, `rule.16.Lower-Expr-Error@L57588`, `diag.16.LiteralAndNameExpressions@L57605`
- `grammar.16.AccessAndPlaceExpressions@L57622`, `req.16.AccessPostfixOwnership@L57638`, `rule.16.Postfix-Field@L57653`, `rule.16.Postfix-TupleIndex@L57669`, `rule.16.Postfix-Index@L57685`, `def.16.IsPlace@L57701`, `rule.16.Parse-Place-Deref@L57714`, `rule.16.Parse-Place-Postfix@L57730`
- `rule.16.Parse-Place-Err@L57746`, `def.16.AccessPlaceAst@L57764`, `def.16.PlaceForms0@L57777`, `def.16.FieldVisibility@L57790`, `def.16.IndexClassification@L57804`, `rule.16.T-Field-Record@L57821`, `rule.16.T-Field-Record-Perm@L57837`, `rule.16.P-Field-Record@L57853`
- `rule.16.P-Field-Record-Perm@L57869`, `rule.16.T-Tuple-Index@L57885`, `rule.16.T-Tuple-Index-Perm@L57901`, `rule.16.P-Tuple-Index@L57917`, `rule.16.P-Tuple-Index-Perm@L57933`, `rule.16.T-Index-Array@L57949`, `rule.16.T-Index-Array-Dynamic@L57965`, `rule.16.T-Index-Array-Perm@L57981`
- `rule.16.T-Index-Array-Perm-Dynamic@L57997`, `rule.16.T-Index-Slice@L58013`, `rule.16.T-Index-Slice-Perm@L58029`, `rule.16.T-Slice-From-Array@L58045`, `rule.16.T-Slice-From-Array-Perm@L58061`, `rule.16.T-Slice-From-Slice@L58077`, `rule.16.T-Slice-From-Slice-Perm@L58093`, `rule.16.PlaceIndexAndSliceCounterparts@L58109`
- `rule.16.Coerce-Array-Slice@L58122`, `rule.16.Union-DirectAccess-Err@L58138`, `rule.16.ValueUse-NonBitcopyPlace@L58154`, `rule.16.EvalSigma-FieldAccess@L58172`, `rule.16.EvalSigma-TupleAccess@L58188`, `rule.16.EvalSigma-Index@L58204`, `rule.16.EvalSigma-Index-Range@L58220`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58236`
- `rule.16.Lower-Expr-FieldAccess@L58251`, `rule.16.Lower-Expr-TupleAccess@L58267`, `rule.16.Lower-Expr-IndexFamily@L58283`, `rule.16.Lower-Place-Ident@L58296`, `rule.16.Lower-Place-Field@L58311`, `rule.16.Lower-Place-Tuple@L58327`, `rule.16.Lower-Place-Index@L58343`, `rule.16.Lower-Place-Deref@L58359`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58375`, `diag.16.AccessAndPlaceExpressions@L58390`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58403`, `grammar.16.CallExpressions@L58420`, `req.16.QualifiedApplyParenPreResolution@L58439`, `rule.16.Postfix-Call@L58454`, `rule.16.Postfix-Call-TypeArgs@L58470`, `rule.16.Postfix-MethodCall@L58486`
- `rule.16.Parse-Qualified-Apply-Paren@L58502`, `rule.16.ArgumentListParsingFamily@L58518`, `def.16.ArgAst@L58533`, `def.16.CallExprAst@L58546`, `def.16.ArgAccessors@L58559`, `def.16.MovedArg@L58573`, `req.16.QualifiedParenthesizedApplicationResolution@L58588`, `def.16.CallStaticJudgementsAndArgumentTyping@L58607`
- `rule.16.ArgsT-Empty@L58635`, `rule.16.ArgsT-Cons@L58650`, `rule.16.ArgsT-Cons-Ref@L58666`, `rule.16.T-Call-Generic-Infer@L58682`, `rule.16.T-Call@L58698`, `rule.16.Call-Callee-NotFunc@L58714`, `rule.16.Call-ArgCount-Err@L58730`, `rule.16.Call-ArgType-Err@L58746`
- `rule.16.Call-Move-Missing@L58762`, `rule.16.Call-Move-Unexpected@L58778`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58794`, `rule.16.Call-Arg-NotPlace@L58810`, `rule.16.Chk-Call-Generic-Infer@L58826`, `req.16.CallTypeArgsStaticOwnership@L58842`, `req.16.MethodRecordClosureCallStaticOwnership@L58855`, `req.16.ExternProcedureCallsRequireUnsafe@L58868`
- `rule.16.EvalSigma-Call-Closure@L58883`, `rule.16.EvalSigma-Call-RegionProc@L58899`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58915`, `rule.16.EvalSigma-Call-CancelProc@L58931`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58947`, `rule.16.EvalSigma-Call-Proc@L58963`, `rule.16.EvalSigma-Call-Record@L58979`, `rule.16.EvalSigma-MethodCall@L58995`
- `req.16.CallControlPropagation@L59011`, `req.16.MethodCallControlPropagation@L59024`, `req.16.CallTypeArgsEvaluationElaboration@L59037`, `rule.16.Lower-Args-Empty@L59052`, `rule.16.Lower-Args-Cons-Move@L59067`, `rule.16.Lower-Args-Cons-Ref@L59083`, `rule.16.Lower-Expr-Call-Closure@L59099`, `rule.16.Lower-Expr-CallFamily@L59115`
- `rule.16.Lower-MethodCallFamily@L59128`, `req.16.CallTypeArgsLoweringElaboration@L59141`, `diag.16.CallExpressions@L59156`, `grammar.16.OperatorExpressions@L59173`, `req.16.OperatorPrefixSyntaxOwnership@L59204`, `rule.16.ParseRangeFamily@L59219`, `rule.16.ParseLeftChainFamily@L59232`, `rule.16.ParsePowerFamily@L59245`
- `rule.16.Parse-Unary-Prefix@L59258`, `def.16.RangeAndOperatorExprAst@L59276`, `def.16.OperatorSets@L59290`, `def.16.OperatorStaticTypes@L59308`, `rule.16.T-Range-Lift@L59323`, `rule.16.RangeTypingFamily@L59339`, `rule.16.T-Not-Bool@L59352`, `rule.16.T-Not-Int@L59368`
- `rule.16.T-Neg@L59384`, `rule.16.T-Arith@L59400`, `rule.16.T-Bitwise@L59416`, `rule.16.T-Shift@L59432`, `rule.16.T-Compare-Eq@L59448`, `rule.16.T-Compare-Ord@L59464`, `rule.16.T-Logical@L59480`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59498`
- `def.16.OperatorComparisonRuntime@L59520`, `def.16.OperatorBitShiftArithmeticRuntime@L59540`, `def.16.UnaryOperatorRuntime@L59560`, `req.16.FloatUnaryNegationTotality@L59578`, `def.16.BinaryOperatorRuntime@L59591`, `rule.16.EvalSigma-Range@L59612`, `rule.16.EvalSigma-Unary@L59628`, `rule.16.EvalSigma-Bin-And-False@L59644`
- `rule.16.EvalSigma-Bin-And-True@L59660`, `rule.16.EvalSigma-Bin-Or-True@L59676`, `rule.16.EvalSigma-Bin-Or-False@L59692`, `rule.16.EvalSigma-Binary@L59708`, `req.16.OperatorUndefinedAndNaNBehavior@L59724`, `rule.16.Lower-Expr-Unary@L59739`, `rule.16.Lower-Expr-Bin-And@L59755`, `rule.16.Lower-Expr-Bin-Or@L59771`
- `rule.16.Lower-Expr-Binary@L59787`, `rule.16.Lower-Expr-Range@L59803`, `def.16.UnaryOperatorLoweringPanicCheck@L59819`, `rule.16.Lower-UnOp-Ok@L59833`, `rule.16.Lower-UnOp-Panic@L59849`, `req.16.UnaryNegationLoweringOverflowChecks@L59865`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59878`, `diag.16.OperatorExpressions@L59893`
- `grammar.16.CastAndTransmuteExpressions@L59910`, `req.16.WidenPrefixOwnershipForCastTransmute@L59926`, `rule.16.Parse-Cast@L59941`, `rule.16.Parse-CastTail-None@L59957`, `rule.16.Parse-CastTail-As@L59973`, `rule.16.ParseTransmuteExprFamily@L59989`, `req.16.WidenParsingOwnershipForCastTransmute@L60002`, `def.16.CastTransmuteExprAst@L60017`
- `req.16.WidenAstOwnershipForCastTransmute@L60030`, `def.16.CastValidity@L60045`, `rule.16.T-Cast@L60060`, `rule.16.T-Cast-Invalid@L60076`, `rule.16.T-Transmute-SizeEq@L60092`, `rule.16.T-Transmute-AlignEq@L60108`, `rule.16.T-Transmute@L60124`, `rule.16.Transmute-Unsafe-Err@L60140`
- `def.16.ValidTransmuteTarget@L60156`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60173`, `def.16.CastDynamicContext@L60188`, `def.16.CastRuntimeConversionHelpers@L60202`, `rule.16.CastVal-Id@L60236`, `rule.16.CastVal-Int-Int-Signed@L60252`, `rule.16.CastVal-Int-Int-Unsigned@L60268`, `rule.16.CastVal-Int-Float@L60284`
- `req.16.IntToFloatLoweringPreservesSignedness@L60300`, `rule.16.CastVal-Float-Float@L60313`, `rule.16.CastVal-Float-Int@L60329`, `rule.16.CastVal-Bool-Int@L60345`, `rule.16.CastVal-Int-Bool@L60363`, `rule.16.CastVal-Char-U32@L60381`, `rule.16.CastVal-U32-Char@L60397`, `rule.16.EvalSigma-Cast@L60413`
- `rule.16.EvalSigma-Cast-Panic@L60429`, `def.16.TransmuteVal@L60445`, `rule.16.EvalSigma-Transmute@L60458`, `rule.16.EvalSigma-Transmute-Ctrl@L60474`, `req.16.WidenDynamicOwnershipForCastTransmute@L60490`, `rule.16.Lower-Expr-Cast@L60505`, `rule.16.Lower-Expr-Transmute@L60521`, `rule.16.LowerCastTransmuteFamily@L60537`
- `diag.16.CastAndTransmuteExpressions@L60552`, `grammar.16.ConstructionExpressions@L60569`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60591`, `rule.16.Parse-Tuple-Literal@L60606`, `rule.16.Parse-Array-Segment-Elem@L60622`, `rule.16.Parse-Array-Segment-Repeat@L60638`, `rule.16.Parse-Array-Segment-List-Empty@L60654`, `rule.16.Parse-Array-Segment-List-Single@L60669`
- `rule.16.Parse-Array-Segment-List-Comma@L60685`, `rule.16.Parse-Array-Literal@L60701`, `rule.16.Parse-Record-Literal-ModalState@L60717`, `rule.16.Parse-Record-Literal@L60733`, `rule.16.Parse-Qualified-Apply-Brace@L60749`, `rule.16.ConstructionListAndShorthandParsingFamily@L60765`, `def.16.FieldInitAst@L60780`, `def.16.ConstructionExprAst@L60793`
- `def.16.FieldInitNamesAndSet@L60806`, `req.16.QualifiedBraceApplicationResolution@L60820`, `req.16.QualifiedParenApplicationConstructionResolution@L60836`, `rule.16.T-Unit-Literal@L60851`, `rule.16.T-Tuple-Literal@L60866`, `def.16.ArraySegmentLength@L60882`, `rule.16.T-Array-Literal-Segments@L60896`, `def.16.RecordFieldNameSet@L60921`
- `rule.16.T-Record-Literal@L60935`, `rule.16.Record-FieldInit-Dup@L60951`, `rule.16.Record-FieldInit-Missing@L60967`, `rule.16.RecordFieldUnknownNotVisibleFamily@L60983`, `rule.16.Record-Field-NonBitcopy-Move@L60996`, `rule.16.EnumLiteralTypingFamily@L61012`, `def.16.RecordDefaultConstructionEligibility@L61025`, `rule.16.T-Record-Default@L61039`
- `rule.16.Record-Default-Init-Err@L61055`, `rule.16.EvalSigmaTupleConstructionFamily@L61073`, `rule.16.EvalSigmaArrayConstructionFamily@L61086`, `rule.16.EvalSigmaRecordConstructionFamily@L61099`, `rule.16.EvalSigmaEnumConstructionFamily@L61112`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61125`, `rule.16.Lower-Expr-Tuple@L61140`, `rule.16.Lower-Expr-Array@L61156`
- `rule.16.Lower-Expr-Record@L61172`, `rule.16.LowerEnumConstructionFamily@L61188`, `rule.16.Lower-CallIR-RecordCtor@L61201`, `diag.16.ConstructionExpressions@L61219`, `grammar.16.ControlExpressions@L61236`, `req.16.ControlExpressionOwnership@L61260`, `rule.16.Parse-If-Expr@L61276`, `rule.16.Parse-If-Is-Single@L61292`
- `rule.16.Parse-If-Is-CaseList@L61308`, `rule.16.Parse-Loop-Expr@L61324`, `rule.16.Parse-Block-Expr@L61340`, `rule.16.ControlExpressionParsingRemainderFamily@L61356`, `def.16.ControlExprAst@L61371`, `def.16.ControlAstHelpers@L61384`, `def.16.LoopTypeInference@L61398`, `req.16.BlockTypingOwnershipForControlExpressions@L61422`
- `rule.16.T-If@L61441`, `rule.16.T-If-No-Else@L61457`, `rule.16.CheckIfFamily@L61473`, `req.16.PatternTypingOwnershipForControlExpressions@L61486`, `rule.16.T-If-Is@L61502`, `rule.16.T-If-Is-No-Else@L61518`, `rule.16.IfCaseTypingFamily@L61534`, `rule.16.CheckIfIsAndIfCaseFamily@L61547`
- `req.16.LoopInvariantTypingOwnership@L61561`, `rule.16.T-Loop-Infinite@L61574`, `rule.16.T-Loop-Conditional@L61590`, `rule.16.T-Loop-Iter@L61606`, `rule.16.AsyncIteratorLoopTypingFamily@L61622`, `rule.16.EvalSigma-If-True@L61637`, `rule.16.EvalSigma-If-False-None@L61653`, `rule.16.EvalSigma-If-False-Some@L61669`
- `rule.16.EvalSigma-If-Ctrl@L61685`, `rule.16.EvalSigma-If-Is@L61701`, `rule.16.EvalSigma-If-Is-Ctrl@L61717`, `rule.16.EvalSigma-If-Cases@L61733`, `rule.16.EvalSigma-If-Cases-Ctrl@L61749`, `rule.16.EvalIfCasesFamily@L61765`, `rule.16.EvalSigma-Block@L61778`, `def.16.LoopIterableTypePredicates@L61794`
- `def.16.LoopIteratorRuntime@L61812`, `def.16.LoopIterJudgement@L61845`, `rule.16.EvalSigma-Loop-Infinite-Step@L61858`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61874`, `rule.16.EvalSigma-Loop-Infinite-Break@L61890`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61906`, `rule.16.EvalSigma-Loop-Cond-False@L61922`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61938`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61954`, `rule.16.EvalSigma-Loop-Cond-Break@L61970`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L61986`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L62002`, `rule.16.EvalSigma-Loop-Iter@L62018`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L62034`, `rule.16.LoopIter-Done@L62050`, `rule.16.LoopIter-Step-Val@L62066`
- `rule.16.LoopIter-Step-Continue@L62082`, `rule.16.LoopIter-Step-Break@L62098`, `rule.16.LoopIter-Step-Ctrl@L62114`, `rule.16.Lower-Expr-If@L62132`, `rule.16.Lower-Expr-If-Is@L62148`, `rule.16.Lower-Expr-If-Cases@L62164`, `rule.16.LowerLoopExpressionFamily@L62180`, `rule.16.Lower-Expr-Block@L62193`
- `req.16.ControlExpressionLoweringOwnership@L62209`, `diag.16.ControlExpressions@L62224`, `req.16.ControlExpressionDiagnosticOwnership@L62237`, `grammar.16.EffectfulCoreExpressions@L62255`, `req.16.RegionAliasAllocRewrite@L62275`, `rule.16.Parse-Unary-Deref@L62290`, `rule.16.Parse-Unary-AddressOf@L62306`, `rule.16.Parse-Unary-Move@L62322`
- `rule.16.Postfix-Propagate@L62338`, `rule.16.Parse-Alloc-Implicit@L62354`, `rule.16.Parse-Unsafe-Expr@L62370`, `def.16.EffectfulCoreExprAst@L62388`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62401`, `def.16.AddressOfStaticHelpers@L62420`, `rule.16.T-Unsafe-Expr@L62435`, `rule.16.Chk-Unsafe-Expr@L62451`
- `rule.16.T-AddrOf@L62467`, `rule.16.T-Deref-Ptr@L62483`, `rule.16.T-Deref-Raw@L62499`, `rule.16.DerefPlaceTypingFamily@L62515`, `rule.16.T-Move@L62528`, `rule.16.T-Alloc-Explicit@L62544`, `rule.16.T-Alloc-Implicit@L62560`, `def.16.SuccessMember@L62576`
- `rule.16.T-Propagate@L62589`, `def.16.SuccessMemberAsync@L62605`, `rule.16.T-Async-Try@L62618`, `rule.16.Async-Try-Infallible-Err@L62634`, `rule.16.EvalSigma-UnsafeBlock@L62652`, `rule.16.EvalSigma-AddressOf@L62668`, `rule.16.EvalSigma-Deref@L62684`, `rule.16.EvalSigma-Move@L62700`
- `rule.16.EvalSigma-Alloc-Implicit@L62716`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62732`, `rule.16.EvalSigma-Alloc-Explicit@L62748`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62764`, `rule.16.EvalSigma-Propagate-Success@L62780`, `rule.16.EvalSigma-Propagate-Success-Async@L62796`, `rule.16.EvalSigma-Propagate-Error@L62812`, `rule.16.EvalSigma-Propagate-Error-Async@L62828`
- `rule.16.EvalSigma-Propagate-Ctrl@L62845`, `def.16.ExprStateAndTerminalExpr@L62861`, `rule.16.StepSigma-Pure@L62876`, `rule.16.StepSigma-Alloc-Implicit@L62892`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62908`, `rule.16.StepSigma-Alloc-Explicit@L62924`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62940`, `rule.16.StepSigma-Block@L62956`
- `rule.16.StepSigma-UnsafeBlock@L62972`, `rule.16.StepSigma-Loop@L62988`, `rule.16.StepSigma-Stateful-Other@L63004`, `rule.16.Lower-Expr-UnsafeBlock@L63022`, `rule.16.Lower-Expr-Move@L63038`, `rule.16.Lower-Expr-AddressOf@L63054`, `rule.16.Lower-Expr-Deref@L63070`, `rule.16.Lower-Expr-Alloc@L63086`
- `rule.16.Lower-Expr-Propagate-Success@L63102`, `rule.16.Lower-Expr-Propagate-Return@L63118`, `req.16.EffectfulCoreLoweringMechanics@L63134`, `diag.16.EffectfulCoreExpressions@L63149`, `grammar.16.ClosureAndPipelineExpressions@L63166`, `req.16.ClosureParamTrailingComma@L63185`, `req.16.ClosureUnionParamParentheses@L63198`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63211`
- `rule.16.Parse-Pipeline@L63226`, `rule.16.Parse-PipelineTail-Stop@L63242`, `rule.16.Parse-PipelineTail-Cons@L63258`, `rule.16.Parse-Closure-Expr@L63274`, `rule.16.Parse-Closure-Expr-Empty@L63290`, `rule.16.Parse-ClosureParams-Single@L63306`, `rule.16.Parse-ClosureParams-Cons@L63322`, `rule.16.Parse-ClosureParamType-Grouped@L63338`
- `rule.16.Parse-ClosureParamType-Plain@L63354`, `rule.16.Parse-ClosureParam-MoveTyped@L63370`, `rule.16.Parse-ClosureParam-MoveUntyped@L63386`, `rule.16.Parse-ClosureParam-Typed@L63402`, `rule.16.Parse-ClosureParam-Untyped@L63418`, `rule.16.Parse-ClosureRetOpt-Some@L63434`, `rule.16.Parse-ClosureRetOpt-None@L63450`, `rule.16.Parse-ClosureBody-Block@L63466`
- `rule.16.Parse-ClosureBody-Expr@L63482`, `def.16.ClosurePipelineAstForms@L63500`, `def.16.ClosureCaptureSets@L63519`, `def.16.ClosureEscapeClassification@L63539`, `def.16.ClosureParameterAccessors@L63554`, `rule.16.T-Closure-NonCapturing@L63568`, `rule.16.T-Closure-Capturing@L63586`, `rule.16.T-Closure-Escaping@L63605`
- `rule.16.K-Closure-Escape-Type@L63625`, `rule.16.Capture-Const@L63641`, `rule.16.Capture-Shared@L63657`, `rule.16.Capture-Unique-Err@L63673`, `rule.16.T-ClosureCall@L63689`, `rule.16.Infer-Closure-Params@L63705`, `rule.16.Infer-Closure-Params-Err@L63721`, `rule.16.Infer-Closure-Return@L63737`
- `req.16.ClosureSharedDependencyInference@L63753`, `def.16.ClosureCaptureBindingAccessors@L63766`, `rule.16.B-Closure-NonCapturing@L63790`, `rule.16.B-Closure-Capturing@L63806`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63825`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63842`, `rule.16.B-Closure-RefCapture-Moved-Err@L63859`, `rule.16.T-Pipeline@L63876`
- `rule.16.T-Pipeline-NotCallable-Err@L63894`, `rule.16.T-Pipeline-TypeMismatch-Err@L63911`, `rule.16.T-Pipeline-ArgCount-Err@L63929`, `rule.16.B-Pipeline@L63946`, `req.16.ClosureParamInferenceFailure@L63962`, `req.16.ClosureSharedDependencyInferenceRestated@L63975`, `def.16.ClosureEnvironmentRuntimeModel@L63990`, `rule.16.EvalSigma-Closure-NonCapturing@L64026`
- `rule.16.EvalSigma-Closure-Capturing@L64042`, `def.16.MarkMoved@L64060`, `rule.16.EvalSigma-ClosureCall@L64074`, `def.16.ClosureCallRuntimeHelpers@L64092`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64114`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64130`, `req.16.ClosureCallResolvedInternalFormRuntime@L64147`, `req.16.PipelineDesugaring@L64160`
- `rule.16.EvalSigma-Pipeline-Func@L64173`, `rule.16.EvalSigma-Pipeline-Closure@L64190`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64207`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64223`, `def.16.ClosureLoweringCaptureTypes@L64241`, `rule.16.Layout-ClosureEnv@L64257`, `rule.16.Layout-ClosureEnv-Empty@L64273`, `rule.16.Lower-Expr-Closure-NonCapturing@L64289`
- `rule.16.Lower-Expr-Closure-Capturing@L64305`, `def.16.LowerCaptureEnv@L64323`, `def.16.CapturedIdentifierLoweringHelpers@L64343`, `rule.16.Lower-CapturedIdent-Ref@L64358`, `req.16.LowerCapturedIdentRefTemporaries@L64375`, `rule.16.Lower-CapturedIdent-Move@L64388`, `def.16.ClosureEnvParam@L64405`, `def.16.ClosureCodeSig@L64418`
- `rule.16.Lower-Closure-Call@L64435`, `req.16.LowerClosureCallResolvedInternalForm@L64453`, `rule.16.Lower-Expr-Pipeline@L64466`, `def.16.LowerPipelineCallablePredicates@L64484`, `diag.16.ClosureAndPipelineExpressions@L64502`, `diag.16.ExpressionDiagnosticsSupplement@L64517`

#### `spec.patterns`

Count: 161 total; 161 required; 0 recommended; 0 informative. Ledger line span: L64193-L66741.

- `grammar.17.BasicPatterns@L64558`, `rule.17.Parse-Pattern-Literal@L64575`, `rule.17.Parse-Pattern-Wildcard@L64591`, `rule.17.Parse-Pattern-Identifier@L64607`, `def.17.PatternAstForms@L64625`, `def.17.PatternJudgements@L64641`, `def.17.PermWrap@L64654`, `rule.17.Pat-StripPerm@L64669`
- `def.17.PatternNameExtractionJudgement@L64685`, `rule.17.Pat-Ident-Names@L64698`, `rule.17.Pat-Wild@L64712`, `rule.17.Pat-Lit@L64727`, `rule.17.Pat-Dup-R-Err@L64742`, `rule.17.Pat-Wildcard-R@L64760`, `rule.17.Pat-Ident-R@L64775`, `rule.17.Pat-Literal-R@L64790`
- `def.17.PatternBindingEnvironment@L64808`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64823`, `rule.17.Match-Wildcard@L64845`, `rule.17.Match-Ident@L64860`, `rule.17.Match-Literal@L64875`, `req.17.BasicPatternLoweringShared@L64893`, `diag.17.BasicPatterns@L64908`, `grammar.17.TupleRecordPatterns@L64925`
- `req.17.TuplePatternSingleElementSemicolon@L64944`, `rule.17.Parse-Pattern-Tuple@L64959`, `rule.17.Parse-Pattern-Record@L64975`, `rule.17.Parse-TuplePatternElems-Empty@L64991`, `rule.17.Parse-TuplePatternElems-Single@L65007`, `rule.17.Parse-TuplePatternElems-Many@L65023`, `rule.17.Parse-FieldPatternList-Empty@L65039`, `rule.17.Parse-FieldPatternList-Cons@L65055`
- `rule.17.Parse-FieldPattern@L65071`, `rule.17.Parse-FieldPatternTailOpt-None@L65087`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65103`, `rule.17.Parse-FieldPatternTail-End@L65119`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65135`, `rule.17.Parse-FieldPatternTail-Comma@L65151`, `def.17.FieldPatternAstAndAccessors@L65169`, `rule.17.PatNames-TuplePattern@L65186`
- `rule.17.Pat-Record-Field-Explicit@L65201`, `rule.17.Pat-Record-Field-Implicit@L65217`, `rule.17.PatNames-RecordPattern@L65232`, `rule.17.Pat-Tuple-R-Arity-Err@L65249`, `rule.17.Pat-Tuple-R@L65265`, `rule.17.Pat-Record-R@L65281`, `rule.17.RecordPattern-UnknownField@L65297`, `def.17.MatchRecordJudgement@L65315`
- `rule.17.MatchRecord-Empty@L65329`, `rule.17.MatchRecord-Cons-Implicit@L65344`, `rule.17.MatchRecord-Cons-Explicit@L65360`, `rule.17.Match-Tuple@L65376`, `rule.17.Match-Record@L65392`, `req.17.TupleRecordPatternLoweringShared@L65410`, `diag.17.TupleRecordPatterns@L65425`, `grammar.17.EnumModalPatterns@L65442`
- `req.17.EnumPayloadSingleElementTuple@L65460`, `rule.17.Parse-Pattern-Enum@L65475`, `rule.17.Parse-Pattern-Modal@L65491`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65507`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65523`, `rule.17.Parse-EnumPayloadPatternElems-One@L65539`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65555`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65571`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65587`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65603`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65619`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65635`, `def.17.EnumModalPayloadPatterns@L65653`, `rule.17.Pat-Enum-None@L65667`, `rule.17.Pat-Enum-Tuple@L65682`, `rule.17.Pat-Enum-Record@L65698`
- `rule.17.Pat-Modal-None@L65714`, `rule.17.Pat-Modal-Record@L65729`, `rule.17.Pat-Enum-Unit-R@L65747`, `rule.17.Pat-Enum-Tuple-R@L65763`, `rule.17.Pat-Enum-Record-R@L65779`, `rule.17.Pat-Modal-R@L65795`, `rule.17.Pat-Modal-State-R@L65811`, `def.17.MatchModalJudgement@L65829`
- `rule.17.Match-Modal-Empty@L65842`, `rule.17.Match-Modal-Record@L65857`, `rule.17.Match-Enum-Unit@L65873`, `rule.17.Match-Enum-Tuple@L65889`, `rule.17.Match-Enum-Record@L65905`, `rule.17.Match-Modal-General@L65921`, `rule.17.Match-Modal-State@L65937`, `req.17.EnumModalPatternLoweringShared@L65955`
- `diag.17.EnumModalPatterns@L65970`, `grammar.17.RangePatterns@L65987`, `rule.17.Parse-Pattern@L66004`, `rule.17.Parse-Pattern-Err@L66020`, `rule.17.Parse-Pattern-Range-None@L66036`, `rule.17.Parse-Pattern-Range@L66052`, `def.17.RangePatternAst@L66070`, `rule.17.Pat-Range-R@L66086`
- `rule.17.RangePattern-NonConst@L66102`, `rule.17.RangePattern-Empty@L66118`, `def.17.ConstPat@L66136`, `rule.17.Match-Range-Inclusive@L66149`, `rule.17.Match-Range-Exclusive@L66165`, `req.17.RangePatternLoweringShared@L66182`, `diag.17.RangePatterns@L66197`, `grammar.17.CaseClauses@L66214`
- `def.17.CaseClauseParsingGroup@L66232`, `rule.17.Parse-IfCases-Cons@L66245`, `rule.17.Parse-IfCase@L66261`, `rule.17.Parse-IfCasesTail-End@L66277`, `rule.17.Parse-IfCasesTail-Else@L66293`, `rule.17.Parse-IfCasesTail-Cons@L66309`, `def.17.IfCaseAst@L66327`, `def.17.BindOrder@L66340`
- `req.17.CaseBodyTypingScope@L66355`, `def.17.IfCaseEvaluationJudgements@L66370`, `rule.17.EvalIfCase-Fail@L66384`, `rule.17.EvalIfCase-Hit@L66400`, `rule.17.EvalIfCases-Head@L66416`, `rule.17.EvalIfCases-Tail@L66432`, `rule.17.EvalIfCases-Else@L66448`, `rule.17.EvalIfCases-None@L66464`
- `def.17.PatternLoweringJudgements@L66482`, `rule.17.Lower-Pat-Correctness@L66496`, `def.17.IfCaseValueCorrect@L66512`, `rule.17.Lower-IfCases-Correctness@L66525`, `def.17.PatternTagHelpers@L66541`, `rule.17.TagOf-Enum@L66557`, `rule.17.TagOf-Modal@L66573`, `rule.17.Lower-BindList-Empty@L66589`
- `rule.17.Lower-BindList-Cons@L66604`, `rule.17.Lower-Pat-General@L66620`, `rule.17.Lower-Pat-Err@L66636`, `rule.17.Lower-IfCases@L66652`, `diag.17.CaseClauses@L66670`, `req.17.ExhaustivenessNoSyntax@L66687`, `req.17.ExhaustivenessNotParserOwned@L66702`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66717`
- `def.17.EnumCaseCoverageHelpers@L66739`, `def.17.ModalCaseCoverageHelpers@L66753`, `def.17.UnionCaseCoverageHelpers@L66766`, `def.17.EnumCaseAnalysisGroup@L66783`, `rule.17.T-IfCase-Enum@L66796`, `def.17.ModalCaseAnalysisGroup@L66812`, `rule.17.T-IfCase-Modal@L66825`, `rule.17.IfCase-Modal-NonExhaustive@L66841`
- `def.17.UnionCaseAnalysisGroup@L66857`, `rule.17.T-IfCase-Union@L66870`, `rule.17.IfCase-Union-NonExhaustive@L66886`, `rule.17.Chk-IfCase-Union@L66902`, `def.17.OtherCaseAnalysisGroup@L66918`, `rule.17.T-IfCase-Other@L66931`, `rule.17.Chk-IfCase-Enum@L66947`, `rule.17.IfCase-Enum-NonExhaustive@L66963`
- `rule.17.Chk-IfCase-Modal@L66979`, `rule.17.Chk-IfCase-Other@L66995`, `rule.17.Chk-IfIs@L67011`, `rule.17.Chk-IfIs-No-Else@L67027`, `rule.17.IfCase-Unreachable@L67043`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L67061`, `req.17.ExhaustivenessNoAdditionalLowering@L67076`, `diag.17.ExhaustivenessAndReachability@L67091`
- `diag.17.PatternDiagnosticsSupplement@L67106`
- `grammar.17.BasicPatterns@L64558`, `rule.17.Parse-Pattern-Literal@L64575`, `rule.17.Parse-Pattern-Wildcard@L64591`, `rule.17.Parse-Pattern-Identifier@L64607`, `def.17.PatternAstForms@L64625`, `def.17.PatternJudgements@L64641`, `def.17.PermWrap@L64654`, `rule.17.Pat-StripPerm@L64669`
- `def.17.PatternNameExtractionJudgement@L64685`, `rule.17.Pat-Ident-Names@L64698`, `rule.17.Pat-Wild@L64712`, `rule.17.Pat-Lit@L64727`, `rule.17.Pat-Dup-R-Err@L64742`, `rule.17.Pat-Wildcard-R@L64760`, `rule.17.Pat-Ident-R@L64775`, `rule.17.Pat-Literal-R@L64790`
- `def.17.PatternBindingEnvironment@L64808`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64823`, `rule.17.Match-Wildcard@L64845`, `rule.17.Match-Ident@L64860`, `rule.17.Match-Literal@L64875`, `req.17.BasicPatternLoweringShared@L64893`, `diag.17.BasicPatterns@L64908`, `grammar.17.TupleRecordPatterns@L64925`
- `req.17.TuplePatternSingleElementSemicolon@L64944`, `rule.17.Parse-Pattern-Tuple@L64959`, `rule.17.Parse-Pattern-Record@L64975`, `rule.17.Parse-TuplePatternElems-Empty@L64991`, `rule.17.Parse-TuplePatternElems-Single@L65007`, `rule.17.Parse-TuplePatternElems-Many@L65023`, `rule.17.Parse-FieldPatternList-Empty@L65039`, `rule.17.Parse-FieldPatternList-Cons@L65055`
- `rule.17.Parse-FieldPattern@L65071`, `rule.17.Parse-FieldPatternTailOpt-None@L65087`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65103`, `rule.17.Parse-FieldPatternTail-End@L65119`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65135`, `rule.17.Parse-FieldPatternTail-Comma@L65151`, `def.17.FieldPatternAstAndAccessors@L65169`, `rule.17.PatNames-TuplePattern@L65186`
- `rule.17.Pat-Record-Field-Explicit@L65201`, `rule.17.Pat-Record-Field-Implicit@L65217`, `rule.17.PatNames-RecordPattern@L65232`, `rule.17.Pat-Tuple-R-Arity-Err@L65249`, `rule.17.Pat-Tuple-R@L65265`, `rule.17.Pat-Record-R@L65281`, `rule.17.RecordPattern-UnknownField@L65297`, `def.17.MatchRecordJudgement@L65315`
- `rule.17.MatchRecord-Empty@L65329`, `rule.17.MatchRecord-Cons-Implicit@L65344`, `rule.17.MatchRecord-Cons-Explicit@L65360`, `rule.17.Match-Tuple@L65376`, `rule.17.Match-Record@L65392`, `req.17.TupleRecordPatternLoweringShared@L65410`, `diag.17.TupleRecordPatterns@L65425`, `grammar.17.EnumModalPatterns@L65442`
- `req.17.EnumPayloadSingleElementTuple@L65460`, `rule.17.Parse-Pattern-Enum@L65475`, `rule.17.Parse-Pattern-Modal@L65491`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65507`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65523`, `rule.17.Parse-EnumPayloadPatternElems-One@L65539`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65555`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65571`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65587`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65603`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65619`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65635`, `def.17.EnumModalPayloadPatterns@L65653`, `rule.17.Pat-Enum-None@L65667`, `rule.17.Pat-Enum-Tuple@L65682`, `rule.17.Pat-Enum-Record@L65698`
- `rule.17.Pat-Modal-None@L65714`, `rule.17.Pat-Modal-Record@L65729`, `rule.17.Pat-Enum-Unit-R@L65747`, `rule.17.Pat-Enum-Tuple-R@L65763`, `rule.17.Pat-Enum-Record-R@L65779`, `rule.17.Pat-Modal-R@L65795`, `rule.17.Pat-Modal-State-R@L65811`, `def.17.MatchModalJudgement@L65829`
- `rule.17.Match-Modal-Empty@L65842`, `rule.17.Match-Modal-Record@L65857`, `rule.17.Match-Enum-Unit@L65873`, `rule.17.Match-Enum-Tuple@L65889`, `rule.17.Match-Enum-Record@L65905`, `rule.17.Match-Modal-General@L65921`, `rule.17.Match-Modal-State@L65937`, `req.17.EnumModalPatternLoweringShared@L65955`
- `diag.17.EnumModalPatterns@L65970`, `grammar.17.RangePatterns@L65987`, `rule.17.Parse-Pattern@L66004`, `rule.17.Parse-Pattern-Err@L66020`, `rule.17.Parse-Pattern-Range-None@L66036`, `rule.17.Parse-Pattern-Range@L66052`, `def.17.RangePatternAst@L66070`, `rule.17.Pat-Range-R@L66086`
- `rule.17.RangePattern-NonConst@L66102`, `rule.17.RangePattern-Empty@L66118`, `def.17.ConstPat@L66136`, `rule.17.Match-Range-Inclusive@L66149`, `rule.17.Match-Range-Exclusive@L66165`, `req.17.RangePatternLoweringShared@L66182`, `diag.17.RangePatterns@L66197`, `grammar.17.CaseClauses@L66214`
- `def.17.CaseClauseParsingGroup@L66232`, `rule.17.Parse-IfCases-Cons@L66245`, `rule.17.Parse-IfCase@L66261`, `rule.17.Parse-IfCasesTail-End@L66277`, `rule.17.Parse-IfCasesTail-Else@L66293`, `rule.17.Parse-IfCasesTail-Cons@L66309`, `def.17.IfCaseAst@L66327`, `def.17.BindOrder@L66340`
- `req.17.CaseBodyTypingScope@L66355`, `def.17.IfCaseEvaluationJudgements@L66370`, `rule.17.EvalIfCase-Fail@L66384`, `rule.17.EvalIfCase-Hit@L66400`, `rule.17.EvalIfCases-Head@L66416`, `rule.17.EvalIfCases-Tail@L66432`, `rule.17.EvalIfCases-Else@L66448`, `rule.17.EvalIfCases-None@L66464`
- `def.17.PatternLoweringJudgements@L66482`, `rule.17.Lower-Pat-Correctness@L66496`, `def.17.IfCaseValueCorrect@L66512`, `rule.17.Lower-IfCases-Correctness@L66525`, `def.17.PatternTagHelpers@L66541`, `rule.17.TagOf-Enum@L66557`, `rule.17.TagOf-Modal@L66573`, `rule.17.Lower-BindList-Empty@L66589`
- `rule.17.Lower-BindList-Cons@L66604`, `rule.17.Lower-Pat-General@L66620`, `rule.17.Lower-Pat-Err@L66636`, `rule.17.Lower-IfCases@L66652`, `diag.17.CaseClauses@L66670`, `req.17.ExhaustivenessNoSyntax@L66687`, `req.17.ExhaustivenessNotParserOwned@L66702`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66717`
- `def.17.EnumCaseCoverageHelpers@L66739`, `def.17.ModalCaseCoverageHelpers@L66753`, `def.17.UnionCaseCoverageHelpers@L66766`, `def.17.EnumCaseAnalysisGroup@L66783`, `rule.17.T-IfCase-Enum@L66796`, `def.17.ModalCaseAnalysisGroup@L66812`, `rule.17.T-IfCase-Modal@L66825`, `rule.17.IfCase-Modal-NonExhaustive@L66841`
- `def.17.UnionCaseAnalysisGroup@L66857`, `rule.17.T-IfCase-Union@L66870`, `rule.17.IfCase-Union-NonExhaustive@L66886`, `rule.17.Chk-IfCase-Union@L66902`, `def.17.OtherCaseAnalysisGroup@L66918`, `rule.17.T-IfCase-Other@L66931`, `rule.17.Chk-IfCase-Enum@L66947`, `rule.17.IfCase-Enum-NonExhaustive@L66963`
- `rule.17.Chk-IfCase-Modal@L66979`, `rule.17.Chk-IfCase-Other@L66995`, `rule.17.Chk-IfIs@L67011`, `rule.17.Chk-IfIs-No-Else@L67027`, `rule.17.IfCase-Unreachable@L67043`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L67061`, `req.17.ExhaustivenessNoAdditionalLowering@L67076`, `diag.17.ExhaustivenessAndReachability@L67091`
- `diag.17.PatternDiagnosticsSupplement@L67106`

#### `spec.statements`

Count: 260 total; 260 required; 0 recommended; 0 informative. Ledger line span: L66771-L70869.

- `grammar.18.Blocks@L67136`, `req.18.BlockStatementExternalDefinitions@L67166`, `def.18.StatementTerminators@L67182`, `def.18.AttachStmtAttrs@L67197`, `rule.18.Parse-Statement@L67210`, `rule.18.Parse-Statement-Err@L67226`, `rule.18.Parse-Block@L67242`, `def.18.RequiredStatementTerminators@L67258`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67271`, `rule.18.ConsumeTerminatorOpt-Req-No@L67287`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67303`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67319`, `def.18.SkipNL@L67335`, `rule.18.ParseStmtSeq-End@L67349`, `rule.18.ParseStmtSeq-TailExpr@L67365`, `rule.18.ParseStmtSeq-Cons@L67381`
- `def.18.SyncStmt@L67397`, `def.18.StatementAstForms@L67412`, `def.18.LastStmtAndResultType@L67425`, `def.18.BindingEnvironmentHelpers@L67444`, `def.18.StatementTypingJudgements@L67461`, `def.18.LoopFlag@L67474`, `def.18.ScopeStackTypeHelpers@L67487`, `rule.18.T-ErrorStmt@L67501`
- `rule.18.BlockInfo-Res@L67516`, `rule.18.BlockInfo-Res-Err@L67532`, `rule.18.BlockInfo-Tail@L67548`, `rule.18.BlockInfo-ReturnTail@L67564`, `rule.18.BlockInfo-Unit@L67580`, `rule.18.T-Block@L67596`, `req.18.BlockCheckingModeValidation@L67612`, `req.18.BlockExprExpressionFormOwnership@L67625`
- `def.18.StatementExecutionJudgements@L67640`, `def.18.ControlAndStatementOutcomes@L67653`, `def.18.BlockExitOutcome@L67672`, `def.18.BlockExit@L67688`, `def.18.EvalBlockBodySigma@L67701`, `def.18.EvalBlockSigma@L67719`, `def.18.EvalBlockBindSigma@L67732`, `def.18.EvalInScopeSigma@L67745`
- `def.18.PlaceEvaluationHelpersGroup@L67758`, `def.18.PlaceJudgements@L67771`, `rule.18.ExecSeq-Empty@L67785`, `rule.18.ExecSeq-Cons-Ok@L67800`, `rule.18.ExecSeq-Cons-Ctrl@L67816`, `rule.18.ExecSigma-Error@L67832`, `def.18.ExecState@L67847`, `rule.18.Step-Exec-Other-Ok@L67860`
- `rule.18.Step-Exec-Other-Ctrl@L67876`, `rule.18.Step-ExecSeq-Ok@L67892`, `rule.18.Step-ExecSeq-Ctrl@L67908`, `rule.18.Step-Exec-Defer@L67924`, `req.18.BlockExprEvalDelegatesToBlock@L67940`, `def.18.LowerStatementJudgements@L67955`, `rule.18.Lower-Stmt-Correctness@L67968`, `rule.18.Lower-Block-Correctness@L67984`
- `def.18.StatementLoweringTotality@L68000`, `rule.18.Lower-StmtList-Empty@L68014`, `rule.18.Lower-StmtList-Cons@L68029`, `rule.18.Lower-Block-Tail@L68045`, `rule.18.Lower-Block-Unit@L68061`, `rule.18.Lower-Stmt-Error@L68077`, `req.18.TemporaryCleanupLowering@L68092`, `def.18.BlockLoopLoweringTotality@L68124`
- `rule.18.Lower-Loop-Infinite@L68140`, `rule.18.Lower-Loop-Cond@L68156`, `rule.18.Lower-Loop-Iter@L68172`, `diag.18.Blocks@L68190`, `grammar.18.BindingStatements@L68207`, `rule.18.Parse-Binding-Stmt@L68225`, `rule.18.Parse-BindingAfterLetVar@L68241`, `rule.18.LetOrVarStmt-Let@L68257`
- `rule.18.LetOrVarStmt-Var@L68273`, `def.18.LetOrVarStmtAst@L68291`, `def.18.BindingAstAndAccessors@L68304`, `def.18.IntroEnt@L68327`, `rule.18.IntroAll-Empty@L68340`, `rule.18.IntroAll-Cons@L68355`, `rule.18.IntroAllVar-Empty@L68371`, `rule.18.IntroAllVar-Cons@L68386`
- `rule.18.T-LetStmt-Ann@L68402`, `rule.18.T-LetStmt-Ann-Mismatch@L68418`, `rule.18.T-LetStmt-Infer@L68434`, `rule.18.T-LetStmt-Infer-Err@L68450`, `req.18.VarStmtTypingMirrorsLet@L68466`, `rule.18.Let-Refutable-Pattern-Err@L68479`, `rule.18.B-LetVar-UniqueNonMove-Err@L68495`, `def.18.SuspendUniqueBind@L68511`
- `rule.18.B-LetVar@L68526`, `rule.18.Prov-LetVar-Ordinary@L68542`, `rule.18.Prov-LetVar-Region-Alias@L68558`, `rule.18.Prov-LetVar-Region-Fresh@L68574`, `def.18.BindVal@L68592`, `def.18.BindPatternRuntimeHelpers@L68605`, `rule.18.BindList-Empty@L68619`, `rule.18.BindList-Cons@L68634`
- `def.18.BindPattern@L68650`, `rule.18.ExecSigma-Let@L68663`, `rule.18.ExecSigma-Let-Ctrl@L68679`, `req.18.VarExecutionMirrorsLet@L68695`, `rule.18.Lower-Stmt-Let@L68710`, `rule.18.Lower-Stmt-Var@L68726`, `diag.18.BindingStatements@L68744`, `grammar.18.LocalUsingStatements@L68761`
- `rule.18.Parse-UsingLocal-Stmt@L68778`, `def.18.UsingLocalStmtAst@L68796`, `req.18.UsingLocalUsesUsingAlias@L68811`, `rule.18.T-UsingLocalStmt@L68824`, `rule.18.T-UsingLocalStmt-Err@L68840`, `req.18.UsingLocalAliasIdentity@L68856`, `rule.18.ExecSigma-UsingLocal@L68871`, `req.18.UsingLocalNoRuntimeEffect@L68886`
- `rule.18.Lower-Stmt-UsingLocal@L68901`, `req.18.UsingLocalNoRuntimeIR@L68916`, `diag.18.LocalUsingStatements@L68931`, `grammar.18.AssignmentStatements@L68948`, `rule.18.Parse-Assign-Stmt@L68966`, `rule.18.AssignOrCompound-Assign@L68982`, `rule.18.AssignOrCompound-Compound@L68998`, `def.18.AssignmentAstForms@L69016`
- `def.18.PlaceRoot@L69030`, `rule.18.T-Assign@L69049`, `rule.18.T-CompoundAssign@L69065`, `rule.18.Assign-NotPlace@L69081`, `rule.18.Assign-Immutable-Err@L69097`, `rule.18.Assign-Type-Err@L69113`, `rule.18.Assign-Const-Err@L69129`, `req.18.AssignmentBindingStateRules@L69145`
- `req.18.AssignmentProvenanceRules@L69158`, `req.18.AssignmentProvenanceEscapeFailures@L69171`, `def.18.AssignmentRootBinding@L69186`, `def.18.DropOnAssign@L69201`, `def.18.DropSubvalueJudgement@L69218`, `rule.18.DropSubvalue-Do@L69231`, `rule.18.DropSubvalue-Skip@L69247`, `rule.18.ExecSigma-Assign@L69263`
- `rule.18.ExecSigma-Assign-Ctrl@L69279`, `rule.18.ExecSigma-CompoundAssign@L69295`, `req.18.CompoundAssignControlPropagation@L69311`, `rule.18.Lower-Stmt-Assign@L69326`, `rule.18.Lower-Stmt-CompoundAssign@L69342`, `diag.18.AssignmentStatements@L69360`, `grammar.18.ExpressionStatements@L69377`, `rule.18.Parse-Expr-Stmt@L69394`
- `def.18.ExprStmtAst@L69412`, `rule.18.T-ExprStmt@L69427`, `req.18.ExprStmtStateAndProvenanceRules@L69443`, `rule.18.ExecSigma-ExprStmt@L69458`, `rule.18.Lower-Stmt-Expr@L69476`, `diag.18.ExpressionStatements@L69494`, `grammar.18.Defer@L69511`, `rule.18.Parse-Defer-Stmt@L69528`
- `def.18.DeferStmtAst@L69546`, `rule.18.T-DeferStmt@L69561`, `rule.18.Defer-NonUnit-Err@L69577`, `rule.18.Defer-NonLocal-Err@L69593`, `rule.18.HasNonLocalCtrl-Return@L69609`, `rule.18.HasNonLocalCtrl-Break@L69624`, `rule.18.HasNonLocalCtrl-Continue@L69640`, `req.18.HasNonLocalCtrlPropagation@L69656`
- `def.18.DeferSafe@L69669`, `req.18.DeferStateAndProvenancePreservation@L69682`, `rule.18.ExecSigma-Defer@L69697`, `req.18.DeferCleanupSmallStep@L69713`, `req.18.DeferCleanupBigStep@L69726`, `rule.18.Lower-Stmt-Defer@L69741`, `diag.18.Defer@L69758`, `grammar.18.Region@L69775`
- `rule.18.Parse-Region-Opts-None@L69794`, `rule.18.Parse-Region-Opts-Some@L69810`, `rule.18.Parse-Region-Alias-None@L69826`, `rule.18.Parse-Region-Alias-Some@L69842`, `rule.18.Parse-Region-Stmt@L69858`, `def.18.RegionStmtAst@L69876`, `def.18.RegionTypeAndFreshNameHelpers@L69889`, `def.18.RegionOptsExpr@L69903`
- `def.18.RegionBind@L69919`, `rule.18.T-RegionStmt@L69934`, `req.18.AnonymousRegionSyntheticBinding@L69950`, `req.18.RegionBindingState@L69963`, `req.18.RegionProvenance@L69976`, `def.18.BindRegionAlias@L69991`, `rule.18.ExecSigma-Region@L70005`, `rule.18.ExecSigma-Region-Ctrl@L70021`
- `def.18.RegionRelease@L70037`, `rule.18.Step-Exec-Region-Enter@L70050`, `rule.18.Step-Exec-Region-Enter-Ctrl@L70066`, `rule.18.Step-Exec-Region-Body@L70082`, `rule.18.Step-Exec-Region-Exit-Ok@L70098`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70114`, `rule.18.Lower-Stmt-Region@L70132`, `diag.18.Region@L70150`
- `grammar.18.Frame@L70167`, `rule.18.Parse-Frame-Stmt@L70184`, `rule.18.Parse-Frame-Explicit@L70200`, `def.18.FrameStmtAst@L70218`, `def.18.InnermostActiveRegion@L70231`, `def.18.FrameBind@L70247`, `rule.18.T-FrameStmt-Implicit@L70264`, `rule.18.T-FrameStmt-Explicit@L70280`
- `rule.18.Frame-NoActiveRegion-Err@L70296`, `rule.18.Frame-Target-NotActive-Err@L70312`, `req.18.FrameSyntheticRegionBinding@L70328`, `req.18.FrameBindingState@L70341`, `req.18.FrameProvenance@L70354`, `def.18.FrameTargetResolution@L70369`, `def.18.FrameEnter@L70383`, `rule.18.ExecSigma-Frame-Implicit@L70396`
- `rule.18.ExecSigma-Frame-Explicit@L70412`, `def.18.FrameReset@L70428`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70441`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70457`, `rule.18.Step-Exec-Frame-Body@L70473`, `rule.18.Step-Exec-Frame-Exit-Ok@L70489`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70505`, `rule.18.Lower-Stmt-Frame-Implicit@L70523`
- `rule.18.Lower-Stmt-Frame-Explicit@L70539`, `diag.18.Frame@L70557`, `grammar.18.ControlTransferStatements@L70574`, `rule.18.Parse-Return-Stmt@L70593`, `rule.18.Parse-Break-Stmt@L70609`, `rule.18.Parse-Continue-Stmt@L70625`, `def.18.ControlTransferAstForms@L70643`, `rule.18.T-Return-Value@L70660`
- `rule.18.T-Return-Unit@L70676`, `rule.18.Return-Async-Type-Err@L70692`, `rule.18.Return-Async-Unit-Err@L70708`, `rule.18.Return-Type-Err@L70724`, `rule.18.Return-Unit-Err@L70740`, `rule.18.T-Break-Value@L70756`, `rule.18.T-Break-Unit@L70772`, `rule.18.Break-Outside-Loop@L70788`
- `rule.18.T-Continue@L70804`, `rule.18.Continue-Outside-Loop@L70820`, `req.18.ControlTransferBindingState@L70836`, `req.18.ControlTransferProvenance@L70849`, `rule.18.ExecSigma-Return@L70864`, `rule.18.ExecSigma-Return-Unit@L70880`, `rule.18.ExecSigma-Return-Ctrl@L70895`, `rule.18.ExecSigma-Break@L70911`
- `rule.18.ExecSigma-Break-Unit@L70927`, `rule.18.ExecSigma-Break-Ctrl@L70942`, `rule.18.ExecSigma-Continue@L70958`, `rule.18.Lower-Stmt-Return@L70975`, `rule.18.Lower-Stmt-Return-Unit@L70991`, `rule.18.Lower-Stmt-Break@L71006`, `rule.18.Lower-Stmt-Break-Unit@L71022`, `rule.18.Lower-Stmt-Continue@L71037`
- `req.18.ControlTransferTemporaryCleanupLowering@L71052`, `diag.18.ControlTransferStatements@L71072`, `grammar.18.UnsafeStatements@L71089`, `rule.18.Parse-Unsafe-Block@L71106`, `def.18.UnsafeBlockStmtAst@L71124`, `rule.18.T-UnsafeStmt@L71139`, `req.18.UnsafeStatementStateAndProvenance@L71155`, `diag.18.UnsafeRequiredOperationOwnership@L71168`
- `rule.18.ExecSigma-UnsafeStmt@L71183`, `rule.18.Lower-Stmt-UnsafeBlock@L71201`, `diag.18.UnsafeStatements@L71219`, `diag.18.StatementDiagnosticsSupplement@L71234`
- `grammar.18.Blocks@L67136`, `req.18.BlockStatementExternalDefinitions@L67166`, `def.18.StatementTerminators@L67182`, `def.18.AttachStmtAttrs@L67197`, `rule.18.Parse-Statement@L67210`, `rule.18.Parse-Statement-Err@L67226`, `rule.18.Parse-Block@L67242`, `def.18.RequiredStatementTerminators@L67258`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67271`, `rule.18.ConsumeTerminatorOpt-Req-No@L67287`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67303`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67319`, `def.18.SkipNL@L67335`, `rule.18.ParseStmtSeq-End@L67349`, `rule.18.ParseStmtSeq-TailExpr@L67365`, `rule.18.ParseStmtSeq-Cons@L67381`
- `def.18.SyncStmt@L67397`, `def.18.StatementAstForms@L67412`, `def.18.LastStmtAndResultType@L67425`, `def.18.BindingEnvironmentHelpers@L67444`, `def.18.StatementTypingJudgements@L67461`, `def.18.LoopFlag@L67474`, `def.18.ScopeStackTypeHelpers@L67487`, `rule.18.T-ErrorStmt@L67501`
- `rule.18.BlockInfo-Res@L67516`, `rule.18.BlockInfo-Res-Err@L67532`, `rule.18.BlockInfo-Tail@L67548`, `rule.18.BlockInfo-ReturnTail@L67564`, `rule.18.BlockInfo-Unit@L67580`, `rule.18.T-Block@L67596`, `req.18.BlockCheckingModeValidation@L67612`, `req.18.BlockExprExpressionFormOwnership@L67625`
- `def.18.StatementExecutionJudgements@L67640`, `def.18.ControlAndStatementOutcomes@L67653`, `def.18.BlockExitOutcome@L67672`, `def.18.BlockExit@L67688`, `def.18.EvalBlockBodySigma@L67701`, `def.18.EvalBlockSigma@L67719`, `def.18.EvalBlockBindSigma@L67732`, `def.18.EvalInScopeSigma@L67745`
- `def.18.PlaceEvaluationHelpersGroup@L67758`, `def.18.PlaceJudgements@L67771`, `rule.18.ExecSeq-Empty@L67785`, `rule.18.ExecSeq-Cons-Ok@L67800`, `rule.18.ExecSeq-Cons-Ctrl@L67816`, `rule.18.ExecSigma-Error@L67832`, `def.18.ExecState@L67847`, `rule.18.Step-Exec-Other-Ok@L67860`
- `rule.18.Step-Exec-Other-Ctrl@L67876`, `rule.18.Step-ExecSeq-Ok@L67892`, `rule.18.Step-ExecSeq-Ctrl@L67908`, `rule.18.Step-Exec-Defer@L67924`, `req.18.BlockExprEvalDelegatesToBlock@L67940`, `def.18.LowerStatementJudgements@L67955`, `rule.18.Lower-Stmt-Correctness@L67968`, `rule.18.Lower-Block-Correctness@L67984`
- `def.18.StatementLoweringTotality@L68000`, `rule.18.Lower-StmtList-Empty@L68014`, `rule.18.Lower-StmtList-Cons@L68029`, `rule.18.Lower-Block-Tail@L68045`, `rule.18.Lower-Block-Unit@L68061`, `rule.18.Lower-Stmt-Error@L68077`, `req.18.TemporaryCleanupLowering@L68092`, `def.18.BlockLoopLoweringTotality@L68124`
- `rule.18.Lower-Loop-Infinite@L68140`, `rule.18.Lower-Loop-Cond@L68156`, `rule.18.Lower-Loop-Iter@L68172`, `diag.18.Blocks@L68190`, `grammar.18.BindingStatements@L68207`, `rule.18.Parse-Binding-Stmt@L68225`, `rule.18.Parse-BindingAfterLetVar@L68241`, `rule.18.LetOrVarStmt-Let@L68257`
- `rule.18.LetOrVarStmt-Var@L68273`, `def.18.LetOrVarStmtAst@L68291`, `def.18.BindingAstAndAccessors@L68304`, `def.18.IntroEnt@L68327`, `rule.18.IntroAll-Empty@L68340`, `rule.18.IntroAll-Cons@L68355`, `rule.18.IntroAllVar-Empty@L68371`, `rule.18.IntroAllVar-Cons@L68386`
- `rule.18.T-LetStmt-Ann@L68402`, `rule.18.T-LetStmt-Ann-Mismatch@L68418`, `rule.18.T-LetStmt-Infer@L68434`, `rule.18.T-LetStmt-Infer-Err@L68450`, `req.18.VarStmtTypingMirrorsLet@L68466`, `rule.18.Let-Refutable-Pattern-Err@L68479`, `rule.18.B-LetVar-UniqueNonMove-Err@L68495`, `def.18.SuspendUniqueBind@L68511`
- `rule.18.B-LetVar@L68526`, `rule.18.Prov-LetVar-Ordinary@L68542`, `rule.18.Prov-LetVar-Region-Alias@L68558`, `rule.18.Prov-LetVar-Region-Fresh@L68574`, `def.18.BindVal@L68592`, `def.18.BindPatternRuntimeHelpers@L68605`, `rule.18.BindList-Empty@L68619`, `rule.18.BindList-Cons@L68634`
- `def.18.BindPattern@L68650`, `rule.18.ExecSigma-Let@L68663`, `rule.18.ExecSigma-Let-Ctrl@L68679`, `req.18.VarExecutionMirrorsLet@L68695`, `rule.18.Lower-Stmt-Let@L68710`, `rule.18.Lower-Stmt-Var@L68726`, `diag.18.BindingStatements@L68744`, `grammar.18.LocalUsingStatements@L68761`
- `rule.18.Parse-UsingLocal-Stmt@L68778`, `def.18.UsingLocalStmtAst@L68796`, `req.18.UsingLocalUsesUsingAlias@L68811`, `rule.18.T-UsingLocalStmt@L68824`, `rule.18.T-UsingLocalStmt-Err@L68840`, `req.18.UsingLocalAliasIdentity@L68856`, `rule.18.ExecSigma-UsingLocal@L68871`, `req.18.UsingLocalNoRuntimeEffect@L68886`
- `rule.18.Lower-Stmt-UsingLocal@L68901`, `req.18.UsingLocalNoRuntimeIR@L68916`, `diag.18.LocalUsingStatements@L68931`, `grammar.18.AssignmentStatements@L68948`, `rule.18.Parse-Assign-Stmt@L68966`, `rule.18.AssignOrCompound-Assign@L68982`, `rule.18.AssignOrCompound-Compound@L68998`, `def.18.AssignmentAstForms@L69016`
- `def.18.PlaceRoot@L69030`, `rule.18.T-Assign@L69049`, `rule.18.T-CompoundAssign@L69065`, `rule.18.Assign-NotPlace@L69081`, `rule.18.Assign-Immutable-Err@L69097`, `rule.18.Assign-Type-Err@L69113`, `rule.18.Assign-Const-Err@L69129`, `req.18.AssignmentBindingStateRules@L69145`
- `req.18.AssignmentProvenanceRules@L69158`, `req.18.AssignmentProvenanceEscapeFailures@L69171`, `def.18.AssignmentRootBinding@L69186`, `def.18.DropOnAssign@L69201`, `def.18.DropSubvalueJudgement@L69218`, `rule.18.DropSubvalue-Do@L69231`, `rule.18.DropSubvalue-Skip@L69247`, `rule.18.ExecSigma-Assign@L69263`
- `rule.18.ExecSigma-Assign-Ctrl@L69279`, `rule.18.ExecSigma-CompoundAssign@L69295`, `req.18.CompoundAssignControlPropagation@L69311`, `rule.18.Lower-Stmt-Assign@L69326`, `rule.18.Lower-Stmt-CompoundAssign@L69342`, `diag.18.AssignmentStatements@L69360`, `grammar.18.ExpressionStatements@L69377`, `rule.18.Parse-Expr-Stmt@L69394`
- `def.18.ExprStmtAst@L69412`, `rule.18.T-ExprStmt@L69427`, `req.18.ExprStmtStateAndProvenanceRules@L69443`, `rule.18.ExecSigma-ExprStmt@L69458`, `rule.18.Lower-Stmt-Expr@L69476`, `diag.18.ExpressionStatements@L69494`, `grammar.18.Defer@L69511`, `rule.18.Parse-Defer-Stmt@L69528`
- `def.18.DeferStmtAst@L69546`, `rule.18.T-DeferStmt@L69561`, `rule.18.Defer-NonUnit-Err@L69577`, `rule.18.Defer-NonLocal-Err@L69593`, `rule.18.HasNonLocalCtrl-Return@L69609`, `rule.18.HasNonLocalCtrl-Break@L69624`, `rule.18.HasNonLocalCtrl-Continue@L69640`, `req.18.HasNonLocalCtrlPropagation@L69656`
- `def.18.DeferSafe@L69669`, `req.18.DeferStateAndProvenancePreservation@L69682`, `rule.18.ExecSigma-Defer@L69697`, `req.18.DeferCleanupSmallStep@L69713`, `req.18.DeferCleanupBigStep@L69726`, `rule.18.Lower-Stmt-Defer@L69741`, `diag.18.Defer@L69758`, `grammar.18.Region@L69775`
- `rule.18.Parse-Region-Opts-None@L69794`, `rule.18.Parse-Region-Opts-Some@L69810`, `rule.18.Parse-Region-Alias-None@L69826`, `rule.18.Parse-Region-Alias-Some@L69842`, `rule.18.Parse-Region-Stmt@L69858`, `def.18.RegionStmtAst@L69876`, `def.18.RegionTypeAndFreshNameHelpers@L69889`, `def.18.RegionOptsExpr@L69903`
- `def.18.RegionBind@L69919`, `rule.18.T-RegionStmt@L69934`, `req.18.AnonymousRegionSyntheticBinding@L69950`, `req.18.RegionBindingState@L69963`, `req.18.RegionProvenance@L69976`, `def.18.BindRegionAlias@L69991`, `rule.18.ExecSigma-Region@L70005`, `rule.18.ExecSigma-Region-Ctrl@L70021`
- `def.18.RegionRelease@L70037`, `rule.18.Step-Exec-Region-Enter@L70050`, `rule.18.Step-Exec-Region-Enter-Ctrl@L70066`, `rule.18.Step-Exec-Region-Body@L70082`, `rule.18.Step-Exec-Region-Exit-Ok@L70098`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70114`, `rule.18.Lower-Stmt-Region@L70132`, `diag.18.Region@L70150`
- `grammar.18.Frame@L70167`, `rule.18.Parse-Frame-Stmt@L70184`, `rule.18.Parse-Frame-Explicit@L70200`, `def.18.FrameStmtAst@L70218`, `def.18.InnermostActiveRegion@L70231`, `def.18.FrameBind@L70247`, `rule.18.T-FrameStmt-Implicit@L70264`, `rule.18.T-FrameStmt-Explicit@L70280`
- `rule.18.Frame-NoActiveRegion-Err@L70296`, `rule.18.Frame-Target-NotActive-Err@L70312`, `req.18.FrameSyntheticRegionBinding@L70328`, `req.18.FrameBindingState@L70341`, `req.18.FrameProvenance@L70354`, `def.18.FrameTargetResolution@L70369`, `def.18.FrameEnter@L70383`, `rule.18.ExecSigma-Frame-Implicit@L70396`
- `rule.18.ExecSigma-Frame-Explicit@L70412`, `def.18.FrameReset@L70428`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70441`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70457`, `rule.18.Step-Exec-Frame-Body@L70473`, `rule.18.Step-Exec-Frame-Exit-Ok@L70489`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70505`, `rule.18.Lower-Stmt-Frame-Implicit@L70523`
- `rule.18.Lower-Stmt-Frame-Explicit@L70539`, `diag.18.Frame@L70557`, `grammar.18.ControlTransferStatements@L70574`, `rule.18.Parse-Return-Stmt@L70593`, `rule.18.Parse-Break-Stmt@L70609`, `rule.18.Parse-Continue-Stmt@L70625`, `def.18.ControlTransferAstForms@L70643`, `rule.18.T-Return-Value@L70660`
- `rule.18.T-Return-Unit@L70676`, `rule.18.Return-Async-Type-Err@L70692`, `rule.18.Return-Async-Unit-Err@L70708`, `rule.18.Return-Type-Err@L70724`, `rule.18.Return-Unit-Err@L70740`, `rule.18.T-Break-Value@L70756`, `rule.18.T-Break-Unit@L70772`, `rule.18.Break-Outside-Loop@L70788`
- `rule.18.T-Continue@L70804`, `rule.18.Continue-Outside-Loop@L70820`, `req.18.ControlTransferBindingState@L70836`, `req.18.ControlTransferProvenance@L70849`, `rule.18.ExecSigma-Return@L70864`, `rule.18.ExecSigma-Return-Unit@L70880`, `rule.18.ExecSigma-Return-Ctrl@L70895`, `rule.18.ExecSigma-Break@L70911`
- `rule.18.ExecSigma-Break-Unit@L70927`, `rule.18.ExecSigma-Break-Ctrl@L70942`, `rule.18.ExecSigma-Continue@L70958`, `rule.18.Lower-Stmt-Return@L70975`, `rule.18.Lower-Stmt-Return-Unit@L70991`, `rule.18.Lower-Stmt-Break@L71006`, `rule.18.Lower-Stmt-Break-Unit@L71022`, `rule.18.Lower-Stmt-Continue@L71037`
- `req.18.ControlTransferTemporaryCleanupLowering@L71052`, `diag.18.ControlTransferStatements@L71072`, `grammar.18.UnsafeStatements@L71089`, `rule.18.Parse-Unsafe-Block@L71106`, `def.18.UnsafeBlockStmtAst@L71124`, `rule.18.T-UnsafeStmt@L71139`, `req.18.UnsafeStatementStateAndProvenance@L71155`, `diag.18.UnsafeRequiredOperationOwnership@L71168`
- `rule.18.ExecSigma-UnsafeStmt@L71183`, `rule.18.Lower-Stmt-UnsafeBlock@L71201`, `diag.18.UnsafeStatements@L71219`, `diag.18.StatementDiagnosticsSupplement@L71234`

#### `spec.key-system`

Count: 185 total; 175 required; 0 recommended; 0 informative. Ledger line span: L70903-L74064.

- `grammar.19.KeyPaths@L71268`, `parse.19.KeyPathRules@L71290`, `ast.19.KeyPathForms@L71316`, `requirement.19.KeyPathWellFormedness@L71341`, `requirement.19.KeyAnalysisSharedOnly@L71354`, `def.19.RootExtraction@L71369`, `def.19.ObjectIdentity@L71393`, `def.19.KeyPathFormation@L71414`
- `requirement.19.PointerDereferenceKeyAccess@L71430`, `requirement.19.SharedDynamicClassObjects@L71448`, `def.19.DynMethods@L71461`, `rule.19.K-Witness-Shared-WF@L71474`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71490`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71505`, `def.19.SharedDynamicMethodCallKeyPath@L71518`, `def.19.KeyLoweringForms@L71535`
- `rule.19.Lower-KeyPath@L71550`, `rule.19.Lower-KeyAccess-Uncovered@L71566`, `rule.19.Lower-KeyAccess-Covered@L71582`, `diagnostics.19.KeyPaths@L71600`, `grammar.19.KeyAcquisitionBlocks@L71624`, `requirement.19.OrderedKeyBlockModifier@L71644`, `parse.19.KeyBlockRules@L71659`, `ast.19.KeyBlockForms@L71692`
- `def.19.KeyTriple@L71725`, `rule.19.K-Mode-Read@L71751`, `rule.19.K-Mode-Write@L71767`, `requirement.19.RestrictiveContextApplies@L71783`, `def.19.ReadContexts@L71796`, `def.19.WriteContexts@L71818`, `def.19.KeyStateContext@L71841`, `def.19.Covered@L71864`
- `requirement.19.ValidKeyContext@L71879`, `rule.19.K-Acquire-New@L71898`, `rule.19.K-Acquire-Covered@L71914`, `requirement.19.KeyAcquisitionEvaluationOrder@L71930`, `rule.19.K-Block-Acquire@L71945`, `rule.19.K-Read-Block-No-Write@L71961`, `requirement.19.KeyCoarseningInlineMarker@L71979`, `rule.19.K-Coarsen-Inline@L71992`
- `requirement.19.FieldKeyBoundary@L72008`, `requirement.19.ClosureDependencySetConsumption@L72023`, `def.19.SharedCaptures@L72036`, `def.19.LocalClosureKeyPath@L72049`, `rule.19.K-Closure-Escape-Keys@L72066`, `requirement.19.EscapingClosureSharedLifetime@L72082`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72095`, `requirement.19.KeyBlockCanonicalOrderReferences@L72114`
- `def.19.KeyBlockRuntimeJudgments@L72127`, `def.19.AcquireKeysSigma@L72140`, `def.19.ReleaseKeysSigma@L72157`, `def.19.ModeOf@L72173`, `rule.19.ExecSigma-KeyBlock@L72190`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72206`, `rule.19.Step-Exec-KeyBlock-Enter@L72222`, `rule.19.Step-Exec-KeyBlock-Body@L72238`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72254`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72270`, `requirement.19.ScopeExitKeyRelease@L72286`, `requirement.19.LocalClosureInvocationSharedCaptures@L72301`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72319`, `def.19.LowerKeyPathsEmpty@L72339`, `def.19.LowerKeyPathsCons@L72352`, `rule.19.Lower-Stmt-KeyBlock@L72365`
- `requirement.19.KeyScopeBound@L72386`, `requirement.19.KeyEscapeRestrictions@L72401`, `requirement.19.FineGrainedKeyLoopWarning@L72416`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72429`, `diagnostics.19.KeyAcquisitionBlocks@L72442`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72472`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72487`, `def.19.PrefixAndDisjoint@L72502`
- `def.19.KeyPathOrdering@L72517`, `def.19.KeyCompatibility@L72552`, `def.19.IndexEquivalence@L72583`, `requirement.19.IndexEquivalenceConservativeSubset@L72606`, `rule.19.K-Disjoint-Safe@L72619`, `rule.19.K-Prefix-Coverage@L72635`, `def.19.DynamicIndexDisjointness@L72653`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72676`
- `rule.19.K-Dynamic-Index-Conflict@L72689`, `def.19.ReadThenWrite@L72707`, `requirement.19.ReadThenWriteDiagnosticSurface@L72724`, `requirement.19.ReadThenWriteOtherWriteForms@L72737`, `rule.19.K-Read-Write-Reject@L72750`, `rule.19.K-RMW-Permitted@L72766`, `rule.19.K-RMW-Explicit-Warn@L72782`, `rule.19.K-RMW-Contention-Warn@L72798`
- `def.19.OrderedComparablePaths@L72814`, `rule.19.K-Ordered-Ok@L72830`, `rule.19.K-Ordered-Base-Err@L72846`, `rule.19.K-Ordered-Redundant-Warn@L72862`, `requirement.19.CanonicalOrderDynamicUse@L72880`, `requirement.19.KeyConflictRuntimeCompatibility@L72893`, `def.19.LowerConflictChecks@L72908`, `rule.19.Lower-Key-ConflictChecks@L72925`
- `diagnostics.19.ConflictDetection@L72943`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72968`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L72983`, `ast.19.NestedReleaseForm@L72998`, `rule.19.K-Nested-Same-Path@L73013`, `def.19.SharedParam@L73034`, `def.19.DirectCalleeAccesses@L73048`, `def.19.CalleeAccessSummary@L73061`
- `def.19.CalleeAccessInstantiation@L73074`, `rule.19.K-Reentrant@L73089`, `requirement.19.UnknownCalleeAccessWarning@L73104`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73119`, `requirement.19.StaleOkSuppressesReleaseWarning@L73134`, `rule.19.K-Release-SameMode-Err@L73147`, `requirement.19.NestedReleaseExecutionSequence@L73165`, `rule.19.K-Release-Sequence@L73184`
- `requirement.19.NestedReleaseInterleavingWindow@L73204`, `def.19.HeldKeyAccessors@L73217`, `def.19.ReleasedKeyState@L73233`, `rule.19.ExecSigma-KeyBlock-Release@L73253`, `rule.19.Lower-Stmt-KeyBlock-Release@L73271`, `diagnostics.19.NestedRelease@L73294`, `grammar.19.SpeculativeExecution@L73317`, `parse.19.SpeculativeBlocks@L73334`
- `ast.19.SpeculativeBlockForm@L73349`, `def.19.SpeculativeSetsAndStates@L73362`, `rule.19.K-Spec-Write-Required@L73390`, `rule.19.K-Spec-Pure-Body@L73406`, `requirement.19.SpeculativePermittedOperations@L73422`, `requirement.19.SpeculativeProhibitedOperations@L73440`, `def.19.IsCallLike@L73460`, `rule.19.K-Spec-No-Nested-Key@L73473`
- `rule.19.K-Spec-No-Impure-Call@L73489`, `rule.19.K-Spec-No-Memory-Ordering@L73505`, `rule.19.K-Spec-No-Wait@L73521`, `rule.19.K-Spec-No-Defer@L73537`, `rule.19.K-Spec-No-Release@L73553`, `rule.19.ExecSigma-KeyBlock-Speculative@L73573`, `def.19.SpecLoop@L73589`, `rule.19.Spec-Start@L73610`
- `rule.19.Spec-Snapshot@L73625`, `rule.19.Spec-Exec-Ok@L73641`, `rule.19.Spec-Exec-Panic@L73657`, `rule.19.Spec-Commit-Success@L73673`, `rule.19.Spec-Commit-Fail-Retry@L73689`, `rule.19.Spec-Commit-Fail-Fallback@L73705`, `rule.19.Spec-Retry@L73721`, `rule.19.Spec-Fallback@L73736`
- `rule.19.SpecBlock-Ok@L73752`, `rule.19.SpecBlock-Panic@L73768`, `def.19.SpeculativeRuntimeHelpers@L73784`, `requirement.19.SpeculativePanicDiscardsWrites@L73805`, `requirement.19.SpeculativeAtomicity@L73818`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73831`, `def.19.SpeculativeIR@L73848`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73861`
- `diagnostics.19.SpeculativeExecution@L73881`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73909`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73924`, `def.19.StaticallySafeConditions@L73939`, `requirement.19.StaticallySafeSoundProofRequired@L73961`, `rule.19.K-Static-Safe@L73980`, `requirement.19.NoRuntimeSyncMeaning@L73996`, `rule.19.K-Static-Required@L74011`
- `requirement.19.RuntimeSynchronizationRequirements@L74029`, `requirement.19.DynamicIndexRuntimeOrdering@L74047`, `requirement.19.DynamicIndexedPathCoarsening@L74066`, `requirement.19.CanonicalOrderDeadlockFreedom@L74081`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74096`, `rule.19.K-Dynamic-Permitted@L74111`, `requirement.19.DynamicContextStaticSafeLowering@L74127`, `diagnostics.19.DynamicKeyVerification@L74142`
- `grammar.19.MemoryOrdering@L74163`, `parse.19.MemoryOrdering@L74183`, `ast.19.MemoryOrderingForms@L74200`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74223`, `def.19.MemoryOrderingLevels@L74238`, `requirement.19.MemoryOrderAttributeAttachment@L74259`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74277`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74290`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74303`, `rule.19.T-Fence@L74316`, `requirement.19.FenceContextAndHeldKeys@L74332`, `requirement.19.FenceEvaluation@L74347`, `requirement.19.FenceOrderingConstraints@L74364`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74381`, `rule.19.Lower-Expr-Fence@L74396`, `rule.19.Lower-Ordered-Access@L74411`
- `diagnostics.19.MemoryOrdering@L74429`
- `grammar.19.KeyPaths@L71268`, `parse.19.KeyPathRules@L71290`, `ast.19.KeyPathForms@L71316`, `requirement.19.KeyPathWellFormedness@L71341`, `requirement.19.KeyAnalysisSharedOnly@L71354`, `def.19.RootExtraction@L71369`, `def.19.ObjectIdentity@L71393`, `def.19.KeyPathFormation@L71414`
- `requirement.19.PointerDereferenceKeyAccess@L71430`, `requirement.19.SharedDynamicClassObjects@L71448`, `def.19.DynMethods@L71461`, `rule.19.K-Witness-Shared-WF@L71474`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71490`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71505`, `def.19.SharedDynamicMethodCallKeyPath@L71518`, `def.19.KeyLoweringForms@L71535`
- `rule.19.Lower-KeyPath@L71550`, `rule.19.Lower-KeyAccess-Uncovered@L71566`, `rule.19.Lower-KeyAccess-Covered@L71582`, `diagnostics.19.KeyPaths@L71600`, `grammar.19.KeyAcquisitionBlocks@L71624`, `requirement.19.OrderedKeyBlockModifier@L71644`, `parse.19.KeyBlockRules@L71659`, `ast.19.KeyBlockForms@L71692`
- `def.19.KeyTriple@L71725`, `rule.19.K-Mode-Read@L71751`, `rule.19.K-Mode-Write@L71767`, `requirement.19.RestrictiveContextApplies@L71783`, `def.19.ReadContexts@L71796`, `def.19.WriteContexts@L71818`, `def.19.KeyStateContext@L71841`, `def.19.Covered@L71864`
- `requirement.19.ValidKeyContext@L71879`, `rule.19.K-Acquire-New@L71898`, `rule.19.K-Acquire-Covered@L71914`, `requirement.19.KeyAcquisitionEvaluationOrder@L71930`, `rule.19.K-Block-Acquire@L71945`, `rule.19.K-Read-Block-No-Write@L71961`, `requirement.19.KeyCoarseningInlineMarker@L71979`, `rule.19.K-Coarsen-Inline@L71992`
- `requirement.19.FieldKeyBoundary@L72008`, `requirement.19.ClosureDependencySetConsumption@L72023`, `def.19.SharedCaptures@L72036`, `def.19.LocalClosureKeyPath@L72049`, `rule.19.K-Closure-Escape-Keys@L72066`, `requirement.19.EscapingClosureSharedLifetime@L72082`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72095`, `requirement.19.KeyBlockCanonicalOrderReferences@L72114`
- `def.19.KeyBlockRuntimeJudgments@L72127`, `def.19.AcquireKeysSigma@L72140`, `def.19.ReleaseKeysSigma@L72157`, `def.19.ModeOf@L72173`, `rule.19.ExecSigma-KeyBlock@L72190`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72206`, `rule.19.Step-Exec-KeyBlock-Enter@L72222`, `rule.19.Step-Exec-KeyBlock-Body@L72238`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72254`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72270`, `requirement.19.ScopeExitKeyRelease@L72286`, `requirement.19.LocalClosureInvocationSharedCaptures@L72301`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72319`, `def.19.LowerKeyPathsEmpty@L72339`, `def.19.LowerKeyPathsCons@L72352`, `rule.19.Lower-Stmt-KeyBlock@L72365`
- `requirement.19.KeyScopeBound@L72386`, `requirement.19.KeyEscapeRestrictions@L72401`, `requirement.19.FineGrainedKeyLoopWarning@L72416`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72429`, `diagnostics.19.KeyAcquisitionBlocks@L72442`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72472`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72487`, `def.19.PrefixAndDisjoint@L72502`
- `def.19.KeyPathOrdering@L72517`, `def.19.KeyCompatibility@L72552`, `def.19.IndexEquivalence@L72583`, `requirement.19.IndexEquivalenceConservativeSubset@L72606`, `rule.19.K-Disjoint-Safe@L72619`, `rule.19.K-Prefix-Coverage@L72635`, `def.19.DynamicIndexDisjointness@L72653`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72676`
- `rule.19.K-Dynamic-Index-Conflict@L72689`, `def.19.ReadThenWrite@L72707`, `requirement.19.ReadThenWriteDiagnosticSurface@L72724`, `requirement.19.ReadThenWriteOtherWriteForms@L72737`, `rule.19.K-Read-Write-Reject@L72750`, `rule.19.K-RMW-Permitted@L72766`, `rule.19.K-RMW-Explicit-Warn@L72782`, `rule.19.K-RMW-Contention-Warn@L72798`
- `def.19.OrderedComparablePaths@L72814`, `rule.19.K-Ordered-Ok@L72830`, `rule.19.K-Ordered-Base-Err@L72846`, `rule.19.K-Ordered-Redundant-Warn@L72862`, `requirement.19.CanonicalOrderDynamicUse@L72880`, `requirement.19.KeyConflictRuntimeCompatibility@L72893`, `def.19.LowerConflictChecks@L72908`, `rule.19.Lower-Key-ConflictChecks@L72925`
- `diagnostics.19.ConflictDetection@L72943`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72968`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L72983`, `ast.19.NestedReleaseForm@L72998`, `rule.19.K-Nested-Same-Path@L73013`, `def.19.SharedParam@L73034`, `def.19.DirectCalleeAccesses@L73048`, `def.19.CalleeAccessSummary@L73061`
- `def.19.CalleeAccessInstantiation@L73074`, `rule.19.K-Reentrant@L73089`, `requirement.19.UnknownCalleeAccessWarning@L73104`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73119`, `requirement.19.StaleOkSuppressesReleaseWarning@L73134`, `rule.19.K-Release-SameMode-Err@L73147`, `requirement.19.NestedReleaseExecutionSequence@L73165`, `rule.19.K-Release-Sequence@L73184`
- `requirement.19.NestedReleaseInterleavingWindow@L73204`, `def.19.HeldKeyAccessors@L73217`, `def.19.ReleasedKeyState@L73233`, `rule.19.ExecSigma-KeyBlock-Release@L73253`, `rule.19.Lower-Stmt-KeyBlock-Release@L73271`, `diagnostics.19.NestedRelease@L73294`, `grammar.19.SpeculativeExecution@L73317`, `parse.19.SpeculativeBlocks@L73334`
- `ast.19.SpeculativeBlockForm@L73349`, `def.19.SpeculativeSetsAndStates@L73362`, `rule.19.K-Spec-Write-Required@L73390`, `rule.19.K-Spec-Pure-Body@L73406`, `requirement.19.SpeculativePermittedOperations@L73422`, `requirement.19.SpeculativeProhibitedOperations@L73440`, `def.19.IsCallLike@L73460`, `rule.19.K-Spec-No-Nested-Key@L73473`
- `rule.19.K-Spec-No-Impure-Call@L73489`, `rule.19.K-Spec-No-Memory-Ordering@L73505`, `rule.19.K-Spec-No-Wait@L73521`, `rule.19.K-Spec-No-Defer@L73537`, `rule.19.K-Spec-No-Release@L73553`, `rule.19.ExecSigma-KeyBlock-Speculative@L73573`, `def.19.SpecLoop@L73589`, `rule.19.Spec-Start@L73610`
- `rule.19.Spec-Snapshot@L73625`, `rule.19.Spec-Exec-Ok@L73641`, `rule.19.Spec-Exec-Panic@L73657`, `rule.19.Spec-Commit-Success@L73673`, `rule.19.Spec-Commit-Fail-Retry@L73689`, `rule.19.Spec-Commit-Fail-Fallback@L73705`, `rule.19.Spec-Retry@L73721`, `rule.19.Spec-Fallback@L73736`
- `rule.19.SpecBlock-Ok@L73752`, `rule.19.SpecBlock-Panic@L73768`, `def.19.SpeculativeRuntimeHelpers@L73784`, `requirement.19.SpeculativePanicDiscardsWrites@L73805`, `requirement.19.SpeculativeAtomicity@L73818`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73831`, `def.19.SpeculativeIR@L73848`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73861`
- `diagnostics.19.SpeculativeExecution@L73881`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73909`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73924`, `def.19.StaticallySafeConditions@L73939`, `requirement.19.StaticallySafeSoundProofRequired@L73961`, `rule.19.K-Static-Safe@L73980`, `requirement.19.NoRuntimeSyncMeaning@L73996`, `rule.19.K-Static-Required@L74011`
- `requirement.19.RuntimeSynchronizationRequirements@L74029`, `requirement.19.DynamicIndexRuntimeOrdering@L74047`, `requirement.19.DynamicIndexedPathCoarsening@L74066`, `requirement.19.CanonicalOrderDeadlockFreedom@L74081`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74096`, `rule.19.K-Dynamic-Permitted@L74111`, `requirement.19.DynamicContextStaticSafeLowering@L74127`, `diagnostics.19.DynamicKeyVerification@L74142`
- `grammar.19.MemoryOrdering@L74163`, `parse.19.MemoryOrdering@L74183`, `ast.19.MemoryOrderingForms@L74200`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74223`, `def.19.MemoryOrderingLevels@L74238`, `requirement.19.MemoryOrderAttributeAttachment@L74259`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74277`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74290`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74303`, `rule.19.T-Fence@L74316`, `requirement.19.FenceContextAndHeldKeys@L74332`, `requirement.19.FenceEvaluation@L74347`, `requirement.19.FenceOrderingConstraints@L74364`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74381`, `rule.19.Lower-Expr-Fence@L74396`, `rule.19.Lower-Ordered-Access@L74411`
- `diagnostics.19.MemoryOrdering@L74429`

#### `spec.structured-parallelism`

Count: 181 total; 180 required; 0 recommended; 0 informative. Ledger line span: L74083-L77316.

- `grammar.20.ParallelBlocks@L74448`, `parse.20.ParallelBlockRules@L74473`, `ast.20.ParallelBlockForms@L74503`, `def.20.ParallelBlockOptionValidation@L74539`, `rule.20.Dim3Const-Err@L74565`, `def.20.ParallelDomainCtorValidation@L74581`, `rule.20.T-Parallel@L74601`, `requirement.20.ParallelBlockWellFormedness@L74617`
- `rule.20.Parallel-Domain-Param-Err@L74634`, `requirement.20.ParallelCancelOptionType@L74650`, `def.20.ParallelState@L74665`, `def.20.ParallelGpuTopologyOptions@L74684`, `def.20.AwaitSpawned@L74716`, `rule.20.EvalSigma-Parallel@L74729`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74745`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74761`
- `requirement.20.ParallelPanicPropagationReference@L74777`, `def.20.ParallelLoweringJudgments@L74792`, `rule.20.Lower-Expr-Parallel@L74805`, `diagnostics.20.ParallelBlocks@L74823`, `requirement.20.ExecutionDomainSyntax@L74844`, `grammar.20.ExecutionDomainExamples@L74857`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74876`, `parse.20.GpuPtrGenericType@L74889`
- `def.20.GpuDomainJudgments@L74904`, `def.20.GpuMemoryForms@L74923`, `def.20.GpuPtrType@L74943`, `def.20.DispatchGpuTopologyComputation@L74957`, `def.20.GpuExecutionTopology@L74980`, `def.20.GpuIntrinsicTable@L75003`, `def.20.GpuRuntimeState@L75029`, `def.20.ExecutionDomainClass@L75055`
- `requirement.20.ExecutionDomainContextMethods@L75075`, `def.20.GpuSafeType@L75102`, `def.20.GpuSafePredicateClauses@L75131`, `rule.20.GpuSafe-Prim@L75146`, `rule.20.GpuSafe-RawPtr@L75162`, `rule.20.GpuSafe-Array@L75178`, `rule.20.GpuSafe-Tuple@L75194`, `rule.20.GpuSafe-Perm@L75210`
- `rule.20.GpuSafe-Record@L75226`, `rule.20.GpuSafe-Enum@L75242`, `rule.20.GpuSafe-StringView@L75258`, `rule.20.GpuSafe-BytesView@L75274`, `rule.20.GpuSafeType-Err@L75290`, `rule.20.GpuSafe-Record-Field-Err@L75306`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75322`, `rule.20.T-GpuIntrinsic@L75338`
- `rule.20.Barrier-Outside-Err@L75354`, `rule.20.GpuIntrinsic-Outside-Err@L75370`, `rule.20.GpuPtr-AddrSpace-Err@L75386`, `requirement.20.ExecutionDomainDispatchableClass@L75402`, `requirement.20.GpuSafeGenericBounds@L75415`, `requirement.20.KeySystemUnavailableInGpuContext@L75428`, `requirement.20.InlineDomainSemantics@L75443`, `def.20.GpuMemoryVisibility@L75461`
- `rule.20.GpuPtr-Deref-Visible@L75477`, `rule.20.GpuPtr-Deref-Err@L75493`, `def.20.GpuTopologyValidity@L75509`, `rule.20.EvalSigma-GPU-Parallel@L75528`, `rule.20.EvalSigma-GPU-Dispatch@L75544`, `rule.20.GpuExecute-Step@L75560`, `rule.20.GpuBarrier-Sync@L75576`, `requirement.20.GpuBarrierWait@L75592`
- `rule.20.EvalSigma-GpuBarrier@L75607`, `rule.20.Barrier-Divergence-Err@L75623`, `rule.20.KeyBlock-GPU-Err@L75639`, `rule.20.WorkgroupSize-Err@L75655`, `rule.20.Lower-Domain-CPU@L75673`, `rule.20.Lower-Domain-GPU@L75688`, `rule.20.Lower-Domain-Inline@L75703`, `rule.20.Lower-Expr-Parallel-GPU@L75718`
- `rule.20.Lower-Expr-GpuBarrier@L75734`, `diagnostics.20.ExecutionDomains@L75751`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75779`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75794`, `requirement.20.CaptureSetComputationReference@L75809`, `def.20.GpuCaptureJudgments@L75831`, `requirement.20.ParallelCapturePermissions@L75848`, `rule.20.Parallel-Closure-Capture-Const@L75865`
- `rule.20.Parallel-Closure-Capture-Shared@L75881`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75897`, `def.20.OuterParallelMoveSelection@L75913`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75927`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75943`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75959`, `requirement.20.ParallelClosuresLocalForKeys@L75975`, `rule.20.GpuCaptureOk-Const@L75988`
- `rule.20.GpuCaptureOk-Unique-Move@L76004`, `rule.20.GpuCapture-Shared-Err@L76020`, `rule.20.GpuCapture-HeapProv-Err@L76036`, `rule.20.GpuCapture-NonGpuSafe-Err@L76052`, `requirement.20.MovedBindingValidityReference@L76068`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76083`, `requirement.20.CaptureSemanticsGenericLowering@L76102`, `diagnostics.20.CaptureSemantics@L76117`
- `grammar.20.Spawn@L76141`, `parse.20.SpawnRules@L76162`, `ast.20.SpawnForms@L76189`, `def.20.SpawnOptionValidation@L76222`, `requirement.20.SpawnRequiresParallelContext@L76241`, `rule.20.T-Spawn@L76254`, `def.20.SpawnHandleAndEnqueue@L76272`, `requirement.20.SpawnEvaluationProcedure@L76289`
- `rule.20.EvalSigma-Spawn@L76309`, `requirement.20.SpawnedResultRetrievalReference@L76325`, `rule.20.Lower-Expr-Spawn@L76340`, `diagnostics.20.Spawn@L76358`, `grammar.20.Dispatch@L76377`, `parse.20.DispatchRules@L76402`, `ast.20.DispatchForms@L76436`, `requirement.20.DispatchRequiresParallelContext@L76479`
- `rule.20.T-Dispatch@L76492`, `rule.20.T-Dispatch-Reduce@L76508`, `rule.20.T-GPU-Dispatch@L76524`, `rule.20.T-GPU-Dispatch-Reduce@L76540`, `def.20.DispatchAccessInference@L76556`, `def.20.DispatchOptionsAndDynamicKeys@L76594`, `rule.20.Dispatch-Infer-Err@L76625`, `rule.20.Dispatch-Outside-Err@L76641`
- `rule.20.Dispatch-Chunk-Type-Err@L76657`, `rule.20.Dispatch-Dependency-Err@L76673`, `rule.20.Dispatch-Reduce-Assoc-Err@L76689`, `rule.20.Dispatch-DynamicKey-Warn@L76705`, `requirement.20.DispatchKeyInferenceRequired@L76721`, `rule.20.DispatchIndexedDisjointness@L76734`, `requirement.20.DispatchReductionAssociativity@L76749`, `requirement.20.DispatchChunkSemanticsStatic@L76762`
- `def.20.DispatchPartitionSpec@L76777`, `def.20.DispatchIndexAndPathDisjointness@L76792`, `def.20.DispatchPartitioning@L76835`, `def.20.DispatchReductionAndChunking@L76856`, `rule.20.EvalSigma-Dispatch@L76878`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76894`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76910`, `def.20.DispatchRun@L76926`
- `rule.20.Lower-Expr-Dispatch@L76947`, `diagnostics.20.Dispatch@L76965`, `requirement.20.CancellationSyntax@L76988`, `requirement.20.CancellationNoAdditionalParsingRules@L77003`, `ast.20.CancelTokenForms@L77018`, `requirement.20.CancelTokenStaticSemantics@L77045`, `requirement.20.CancelTokenParallelAvailability@L77070`, `def.20.CancelRuntimeHelpers@L77083`
- `rule.20.Cancel-New@L77106`, `rule.20.Cancel-Child@L77122`, `rule.20.Cancel-IsCancelled@L77138`, `rule.20.Cancel-DoCancel@L77154`, `rule.20.Cancel-WaitCancelled-Completed@L77170`, `rule.20.Cancel-WaitCancelled-Suspended@L77186`, `requirement.20.CooperativeCancellationBehavior@L77202`, `def.20.CancelIR@L77224`
- `rule.20.Lower-Cancel-New@L77237`, `rule.20.Lower-Cancel-Request@L77252`, `rule.20.Lower-Cancel-Wait@L77267`, `requirement.20.CancellationCheckpointLowering@L77282`, `requirement.20.SpawnDispatchCancellationLowering@L77295`, `diagnostics.20.Cancellation@L77310`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77327`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77342`
- `ast.20.ParallelPanicPropagationInputs@L77357`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77372`, `requirement.20.ParallelWorkItemPanicSemantics@L77387`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77404`, `requirement.20.ParallelPanicCancellationRequest@L77420`, `def.20.FirstCompletedFailure@L77433`, `rule.20.Lower-Parallel-Join-Panic@L77448`, `diagnostics.20.PanicHandling@L77465`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77482`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77497`, `ast.20.DeterminismNestingForms@L77512`, `requirement.20.DispatchDeterminismConditions@L77527`, `requirement.20.OrderedDispatchSequentialSideEffects@L77544`, `requirement.20.NoNestedGpuParallel@L77557`, `requirement.20.NestedParallelRuntimeSemantics@L77572`, `def.20.ParallelDeterministicOrdering@L77593`
- `rule.20.Lower-Deterministic-Dispatch@L77619`, `rule.20.Lower-Nested-Parallel@L77635`, `diagnostics.20.DeterminismAndNesting@L77651`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77668`, `diagnostics.20.StructuredParallelismSupplement@L77681`
- `grammar.20.ParallelBlocks@L74448`, `parse.20.ParallelBlockRules@L74473`, `ast.20.ParallelBlockForms@L74503`, `def.20.ParallelBlockOptionValidation@L74539`, `rule.20.Dim3Const-Err@L74565`, `def.20.ParallelDomainCtorValidation@L74581`, `rule.20.T-Parallel@L74601`, `requirement.20.ParallelBlockWellFormedness@L74617`
- `rule.20.Parallel-Domain-Param-Err@L74634`, `requirement.20.ParallelCancelOptionType@L74650`, `def.20.ParallelState@L74665`, `def.20.ParallelGpuTopologyOptions@L74684`, `def.20.AwaitSpawned@L74716`, `rule.20.EvalSigma-Parallel@L74729`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74745`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74761`
- `requirement.20.ParallelPanicPropagationReference@L74777`, `def.20.ParallelLoweringJudgments@L74792`, `rule.20.Lower-Expr-Parallel@L74805`, `diagnostics.20.ParallelBlocks@L74823`, `requirement.20.ExecutionDomainSyntax@L74844`, `grammar.20.ExecutionDomainExamples@L74857`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74876`, `parse.20.GpuPtrGenericType@L74889`
- `def.20.GpuDomainJudgments@L74904`, `def.20.GpuMemoryForms@L74923`, `def.20.GpuPtrType@L74943`, `def.20.DispatchGpuTopologyComputation@L74957`, `def.20.GpuExecutionTopology@L74980`, `def.20.GpuIntrinsicTable@L75003`, `def.20.GpuRuntimeState@L75029`, `def.20.ExecutionDomainClass@L75055`
- `requirement.20.ExecutionDomainContextMethods@L75075`, `def.20.GpuSafeType@L75102`, `def.20.GpuSafePredicateClauses@L75131`, `rule.20.GpuSafe-Prim@L75146`, `rule.20.GpuSafe-RawPtr@L75162`, `rule.20.GpuSafe-Array@L75178`, `rule.20.GpuSafe-Tuple@L75194`, `rule.20.GpuSafe-Perm@L75210`
- `rule.20.GpuSafe-Record@L75226`, `rule.20.GpuSafe-Enum@L75242`, `rule.20.GpuSafe-StringView@L75258`, `rule.20.GpuSafe-BytesView@L75274`, `rule.20.GpuSafeType-Err@L75290`, `rule.20.GpuSafe-Record-Field-Err@L75306`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75322`, `rule.20.T-GpuIntrinsic@L75338`
- `rule.20.Barrier-Outside-Err@L75354`, `rule.20.GpuIntrinsic-Outside-Err@L75370`, `rule.20.GpuPtr-AddrSpace-Err@L75386`, `requirement.20.ExecutionDomainDispatchableClass@L75402`, `requirement.20.GpuSafeGenericBounds@L75415`, `requirement.20.KeySystemUnavailableInGpuContext@L75428`, `requirement.20.InlineDomainSemantics@L75443`, `def.20.GpuMemoryVisibility@L75461`
- `rule.20.GpuPtr-Deref-Visible@L75477`, `rule.20.GpuPtr-Deref-Err@L75493`, `def.20.GpuTopologyValidity@L75509`, `rule.20.EvalSigma-GPU-Parallel@L75528`, `rule.20.EvalSigma-GPU-Dispatch@L75544`, `rule.20.GpuExecute-Step@L75560`, `rule.20.GpuBarrier-Sync@L75576`, `requirement.20.GpuBarrierWait@L75592`
- `rule.20.EvalSigma-GpuBarrier@L75607`, `rule.20.Barrier-Divergence-Err@L75623`, `rule.20.KeyBlock-GPU-Err@L75639`, `rule.20.WorkgroupSize-Err@L75655`, `rule.20.Lower-Domain-CPU@L75673`, `rule.20.Lower-Domain-GPU@L75688`, `rule.20.Lower-Domain-Inline@L75703`, `rule.20.Lower-Expr-Parallel-GPU@L75718`
- `rule.20.Lower-Expr-GpuBarrier@L75734`, `diagnostics.20.ExecutionDomains@L75751`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75779`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75794`, `requirement.20.CaptureSetComputationReference@L75809`, `def.20.GpuCaptureJudgments@L75831`, `requirement.20.ParallelCapturePermissions@L75848`, `rule.20.Parallel-Closure-Capture-Const@L75865`
- `rule.20.Parallel-Closure-Capture-Shared@L75881`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75897`, `def.20.OuterParallelMoveSelection@L75913`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75927`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75943`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75959`, `requirement.20.ParallelClosuresLocalForKeys@L75975`, `rule.20.GpuCaptureOk-Const@L75988`
- `rule.20.GpuCaptureOk-Unique-Move@L76004`, `rule.20.GpuCapture-Shared-Err@L76020`, `rule.20.GpuCapture-HeapProv-Err@L76036`, `rule.20.GpuCapture-NonGpuSafe-Err@L76052`, `requirement.20.MovedBindingValidityReference@L76068`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76083`, `requirement.20.CaptureSemanticsGenericLowering@L76102`, `diagnostics.20.CaptureSemantics@L76117`
- `grammar.20.Spawn@L76141`, `parse.20.SpawnRules@L76162`, `ast.20.SpawnForms@L76189`, `def.20.SpawnOptionValidation@L76222`, `requirement.20.SpawnRequiresParallelContext@L76241`, `rule.20.T-Spawn@L76254`, `def.20.SpawnHandleAndEnqueue@L76272`, `requirement.20.SpawnEvaluationProcedure@L76289`
- `rule.20.EvalSigma-Spawn@L76309`, `requirement.20.SpawnedResultRetrievalReference@L76325`, `rule.20.Lower-Expr-Spawn@L76340`, `diagnostics.20.Spawn@L76358`, `grammar.20.Dispatch@L76377`, `parse.20.DispatchRules@L76402`, `ast.20.DispatchForms@L76436`, `requirement.20.DispatchRequiresParallelContext@L76479`
- `rule.20.T-Dispatch@L76492`, `rule.20.T-Dispatch-Reduce@L76508`, `rule.20.T-GPU-Dispatch@L76524`, `rule.20.T-GPU-Dispatch-Reduce@L76540`, `def.20.DispatchAccessInference@L76556`, `def.20.DispatchOptionsAndDynamicKeys@L76594`, `rule.20.Dispatch-Infer-Err@L76625`, `rule.20.Dispatch-Outside-Err@L76641`
- `rule.20.Dispatch-Chunk-Type-Err@L76657`, `rule.20.Dispatch-Dependency-Err@L76673`, `rule.20.Dispatch-Reduce-Assoc-Err@L76689`, `rule.20.Dispatch-DynamicKey-Warn@L76705`, `requirement.20.DispatchKeyInferenceRequired@L76721`, `rule.20.DispatchIndexedDisjointness@L76734`, `requirement.20.DispatchReductionAssociativity@L76749`, `requirement.20.DispatchChunkSemanticsStatic@L76762`
- `def.20.DispatchPartitionSpec@L76777`, `def.20.DispatchIndexAndPathDisjointness@L76792`, `def.20.DispatchPartitioning@L76835`, `def.20.DispatchReductionAndChunking@L76856`, `rule.20.EvalSigma-Dispatch@L76878`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76894`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76910`, `def.20.DispatchRun@L76926`
- `rule.20.Lower-Expr-Dispatch@L76947`, `diagnostics.20.Dispatch@L76965`, `requirement.20.CancellationSyntax@L76988`, `requirement.20.CancellationNoAdditionalParsingRules@L77003`, `ast.20.CancelTokenForms@L77018`, `requirement.20.CancelTokenStaticSemantics@L77045`, `requirement.20.CancelTokenParallelAvailability@L77070`, `def.20.CancelRuntimeHelpers@L77083`
- `rule.20.Cancel-New@L77106`, `rule.20.Cancel-Child@L77122`, `rule.20.Cancel-IsCancelled@L77138`, `rule.20.Cancel-DoCancel@L77154`, `rule.20.Cancel-WaitCancelled-Completed@L77170`, `rule.20.Cancel-WaitCancelled-Suspended@L77186`, `requirement.20.CooperativeCancellationBehavior@L77202`, `def.20.CancelIR@L77224`
- `rule.20.Lower-Cancel-New@L77237`, `rule.20.Lower-Cancel-Request@L77252`, `rule.20.Lower-Cancel-Wait@L77267`, `requirement.20.CancellationCheckpointLowering@L77282`, `requirement.20.SpawnDispatchCancellationLowering@L77295`, `diagnostics.20.Cancellation@L77310`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77327`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77342`
- `ast.20.ParallelPanicPropagationInputs@L77357`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77372`, `requirement.20.ParallelWorkItemPanicSemantics@L77387`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77404`, `requirement.20.ParallelPanicCancellationRequest@L77420`, `def.20.FirstCompletedFailure@L77433`, `rule.20.Lower-Parallel-Join-Panic@L77448`, `diagnostics.20.PanicHandling@L77465`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77482`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77497`, `ast.20.DeterminismNestingForms@L77512`, `requirement.20.DispatchDeterminismConditions@L77527`, `requirement.20.OrderedDispatchSequentialSideEffects@L77544`, `requirement.20.NoNestedGpuParallel@L77557`, `requirement.20.NestedParallelRuntimeSemantics@L77572`, `def.20.ParallelDeterministicOrdering@L77593`
- `rule.20.Lower-Deterministic-Dispatch@L77619`, `rule.20.Lower-Nested-Parallel@L77635`, `diagnostics.20.DeterminismAndNesting@L77651`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77668`, `diagnostics.20.StructuredParallelismSupplement@L77681`

#### `spec.async`

Count: 254 total; 253 required; 0 recommended; 0 informative. Ledger line span: L77337-L82007.

- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77702`, `requirement.21.ReservedAsyncTypeConstructors@L77715`, `requirement.21.AsyncParameterDefaults@L77735`, `requirement.21.ReservedAsyncStates@L77748`, `parse.21.AsyncTypes@L77767`, `parse.21.UnappliedAsyncPath@L77782`, `ast.21.AsyncModalDeclaration@L77797`, `ast.21.AsyncAliases@L77868`
- `ast.21.AsyncCombinatorMembers@L77889`, `def.21.AsyncSigAndBodyReturnType@L77909`, `rule.21.Sub-Async@L77936`, `rule.21.WF-Async@L77957`, `rule.21.WF-Async-ArgCount-Err@L77975`, `rule.21.WF-Async-Arg-WF-Err@L77993`, `rule.21.WF-Async-Path-Err@L78011`, `requirement.21.AsyncFailedUninhabitedForNeverError@L78029`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L78044`, `def.21.AsyncTypeLoweringForms@L78061`, `requirement.21.AsyncNeverErrorLowering@L78092`, `rule.21.Lower-Async-Type@L78105`, `rule.21.Lower-Async-Alias@L78125`, `diagnostics.21.AsyncType@L78145`, `grammar.21.SuspensionForms@L78164`, `parse.21.SuspensionFormsPrimaryExpressions@L78183`
- `rule.21.Parse-Wait-Expr@L78198`, `rule.21.Parse-Yield-From-Expr@L78218`, `rule.21.Parse-Yield-Expr@L78241`, `ast.21.SuspensionForms@L78264`, `ast.21.SuspensionFormResolution@L78284`, `ast.21.SuspensionFormEvaluationOrder@L78303`, `rule.21.T-Wait@L78326`, `rule.21.T-Wait-Future@L78344`
- `rule.21.Wait-Handle-Err@L78362`, `rule.21.T-Yield@L78382`, `rule.21.Yield-NotAsync-Err@L78400`, `rule.21.Yield-Out-Err@L78418`, `rule.21.T-Yield-From@L78438`, `rule.21.YieldFrom-NotAsync-Err@L78456`, `rule.21.YieldFrom-Out-Err@L78474`, `rule.21.YieldFrom-In-Err@L78493`
- `rule.21.YieldFrom-ErrType-Err@L78511`, `requirement.21.SuspensionKeyRestrictionsReference@L78529`, `requirement.21.WaitRuntimeSemantics@L78544`, `def.21.WaitRuntimeHelpers@L78566`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78590`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78608`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78627`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78640`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78658`, `rule.21.EvalSigma-Wait-Ctrl@L78677`, `requirement.21.YieldRuntimeSemantics@L78695`, `def.21.ResumptionHelpers@L78715`, `rule.21.EvalSigma-Yield@L78750`, `rule.21.EvalSigma-Yield-Release@L78769`, `rule.21.EvalSigma-Yield-Resume@L78788`, `requirement.21.YieldFromRuntimeSemantics@L78807`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78827`, `rule.21.EvalSigma-YieldFrom-Completed@L78847`, `rule.21.EvalSigma-YieldFrom-Failed@L78865`, `rule.21.EvalSigma-YieldFrom-Resume@L78883`, `def.21.EvalYieldFromContinueSignature@L78902`, `rule.21.EvalYieldFromContinue-Suspended@L78917`, `rule.21.EvalYieldFromContinue-Completed@L78937`, `rule.21.EvalYieldFromContinue-Failed@L78955`
- `def.21.SuspensionLoweringForms@L78975`, `rule.21.Lower-Wait-Spawned@L78996`, `rule.21.Lower-Wait-Tracked@L79014`, `rule.21.Lower-Yield@L79032`, `rule.21.Lower-Yield-Release@L79050`, `requirement.21.YieldReleaseReacquireLowering@L79068`, `rule.21.Lower-YieldFrom@L79081`, `requirement.21.YieldFromEnterLoweringLoop@L79099`
- `diagnostics.21.SuspensionForms@L79117`, `requirement.21.AsyncIterationSyntax@L79142`, `grammar.21.CompositionForms@L79157`, `requirement.21.AsyncMethodCallSurfaces@L79176`, `requirement.21.UntilMethodCallSurface@L79196`, `parse.21.CompositionPrimaryExpressions@L79211`, `rule.21.Parse-Sync-Expr@L79226`, `rule.21.Parse-Race-Expr@L79246`
- `rule.21.Parse-RaceArms-Cons@L79266`, `rule.21.Parse-RaceArm@L79284`, `rule.21.Parse-RaceArmsTail-End@L79304`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79322`, `rule.21.Parse-RaceArmsTail-Comma@L79340`, `rule.21.Parse-RaceHandler-Yield@L79358`, `rule.21.Parse-RaceHandler-Return@L79376`, `rule.21.Parse-All-Expr@L79396`
- `parse.21.CompositionOrdinarySurfaces@L79414`, `ast.21.CompositionForms@L79429`, `ast.21.AsyncIterationLoopForm@L79451`, `ast.21.CompositionMethodCallForms@L79468`, `ast.21.CompositionResolution@L79483`, `ast.21.CompositionEvaluationOrder@L79512`, `rule.21.T-Loop-Iter-Async@L79538`, `rule.21.Loop-Async-Err@L79560`
- `requirement.21.ManualSteppingRequirement@L79578`, `def.21.SyncYieldContainment@L79593`, `rule.21.Sync-Yield-Err@L79609`, `rule.21.Sync-YieldFrom-Err@L79627`, `rule.21.T-Sync@L79645`, `rule.21.Sync-Async-Context-Err@L79663`, `rule.21.Sync-Out-Err@L79681`, `rule.21.Sync-In-Err@L79700`
- `def.21.RaceMode@L79720`, `rule.21.T-Race@L79738`, `rule.21.T-Race-Stream@L79759`, `rule.21.Race-Arity-Err@L79780`, `rule.21.Race-Handler-Mix-Err@L79798`, `rule.21.Race-Operand-Out-Err@L79816`, `rule.21.Race-Operand-Err@L79835`, `rule.21.Race-Stream-Operand-Err@L79854`
- `rule.21.Race-Handler-Type-Err@L79873`, `rule.21.Race-Stream-Handler-Type-Err@L79894`, `rule.21.T-All@L79917`, `rule.21.All-Out-Err@L79936`, `rule.21.All-In-Err@L79954`, `def.21.UntilType@L79972`, `def.21.AsyncCombinatorTypes@L79989`, `requirement.21.AsyncCombinatorMemberLookup@L80010`
- `rule.21.T-Async-Map@L80025`, `rule.21.T-Async-Filter@L80043`, `rule.21.T-Async-Take@L80061`, `rule.21.T-Async-Fold@L80079`, `rule.21.T-Async-Chain@L80097`, `requirement.21.AsyncIterationRuntimeSemantics@L80117`, `requirement.21.ManualSteppingRuntimeSemantics@L80135`, `requirement.21.SyncRuntimeSemantics@L80148`
- `def.21.SyncStepSignature@L80168`, `rule.21.SyncStep-Suspended@L80183`, `rule.21.EvalSigma-Sync-Suspended@L80201`, `rule.21.EvalSigma-Sync-Completed@L80220`, `rule.21.EvalSigma-Sync-Failed@L80238`, `requirement.21.RaceReturnRuntimeSemantics@L80256`, `requirement.21.RaceStreamingRuntimeSemantics@L80274`, `def.21.RaceSelectionAndState@L80295`
- `rule.21.InitRace@L80325`, `def.21.RaceStepReturnSignature@L80344`, `rule.21.RaceStepReturn-Completed@L80359`, `rule.21.RaceStepReturn-Failed@L80379`, `rule.21.RaceStepReturn-Continue@L80398`, `rule.21.EvalSigma-Race-Return@L80418`, `def.21.RaceStepStreamSignature@L80437`, `rule.21.RaceStepStream-Yield-Initial@L80452`
- `rule.21.RaceStepStream-AllComplete@L80472`, `rule.21.RaceStepStream-Failed@L80490`, `rule.21.EvalSigma-Race-Stream@L80509`, `def.21.CancelAllSignature@L80528`, `rule.21.CancelAll@L80543`, `def.21.RaceStreamSuspensionState@L80563`, `rule.21.RaceStepStream-Yield-Resumable@L80583`, `rule.21.ResumeRaceState-Step@L80603`
- `rule.21.ResumeRaceState-Done@L80622`, `rule.21.EvalSigma-Race-Stream-Resume@L80639`, `requirement.21.StreamingRaceResumptionOrder@L80659`, `requirement.21.AllRuntimeSemantics@L80672`, `def.21.AllStateAndInitSignature@L80692`, `rule.21.InitAll@L80708`, `def.21.AllStepSignature@L80727`, `rule.21.AllStep-Complete@L80742`
- `rule.21.AllStep-Failed@L80761`, `rule.21.AllStep-Resume@L80781`, `def.21.AllLoopSignature@L80801`, `rule.21.AllLoop-AllCompleted@L80816`, `rule.21.AllLoop-Failed@L80834`, `rule.21.AllLoop-Continue@L80852`, `rule.21.EvalSigma-All@L80871`, `requirement.21.UntilRuntimeSemantics@L80889`
- `def.21.AsyncCombinatorRuntimeWrappers@L80906`, `rule.21.EvalSigma-Map-Create@L80929`, `rule.21.EvalSigma-Map-Resume-Yield@L80947`, `rule.21.EvalSigma-Map-Resume-Complete@L80965`, `rule.21.EvalSigma-Map-Resume-Failed@L80983`, `rule.21.EvalSigma-Filter-Create@L81001`, `rule.21.EvalSigma-Filter-Resume-Pass@L81019`, `rule.21.EvalSigma-Filter-Resume-Skip@L81037`
- `rule.21.EvalSigma-Filter-Resume-Complete@L81056`, `rule.21.EvalSigma-Take-Create@L81074`, `rule.21.EvalSigma-Take-Resume-Yield@L81092`, `rule.21.EvalSigma-Take-Resume-Done@L81110`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81128`, `rule.21.EvalSigma-Fold-Create@L81146`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81164`, `rule.21.EvalSigma-Fold-Resume-Complete@L81182`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81200`, `rule.21.EvalSigma-Chain-Create@L81218`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81236`, `rule.21.EvalSigma-Chain-Resume-Chained@L81254`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81272`, `def.21.AsyncComposeIR@L81292`, `rule.21.Lower-Expr-Sync@L81305`, `requirement.21.SyncLoopIRSemantics@L81323`
- `rule.21.Lower-Expr-Race-Return@L81336`, `rule.21.Lower-Expr-Race-Stream@L81354`, `requirement.21.RaceInitIRSemantics@L81372`, `requirement.21.RaceResumeIRSemantics@L81388`, `rule.21.Lower-Expr-All@L81401`, `requirement.21.AllJoinIRSemantics@L81417`, `requirement.21.AsyncCombinatorWrapperLowering@L81430`, `rule.21.Lower-Async-Map@L81443`
- `rule.21.Lower-Async-Filter@L81459`, `rule.21.Lower-Async-Take@L81475`, `rule.21.Lower-Async-Fold@L81491`, `rule.21.Lower-Async-Chain@L81507`, `requirement.21.AsyncWrapperLoweringSemantics@L81523`, `diagnostics.21.AsyncCompositionDiagnostics@L81538`, `requirement.21.AsyncStateMachineSyntaxSurface@L81568`, `def.21.AsyncProcedureDefinition@L81581`
- `requirement.21.AsyncStateMachineParsingSurface@L81596`, `def.21.AsyncStateMachineHelperForms@L81613`, `requirement.21.AsyncFrameStoredState@L81642`, `def.21.LiveAcrossSuspension@L81661`, `rule.21.Warn-Async-LargeCapture@L81676`, `rule.21.Warn-Async-LargeCapture-Ok@L81694`, `requirement.21.AsyncLargeCaptureWarningEmission@L81712`, `rule.21.Async-Capture-Err@L81725`
- `rule.21.P-Async-Create@L81743`, `rule.21.Prov-Async-Escape-Err@L81762`, `requirement.21.AsyncErrorPropagationTypingReference@L81781`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81796`, `requirement.21.AsyncSettlementRuntimeSemantics@L81814`, `requirement.21.AsyncResumeRuntimeSemantics@L81831`, `requirement.21.AsyncFailureRuntimeSemantics@L81847`, `def.21.AsyncStateMachineLoweringJudgements@L81867`
- `def.21.AsyncStateMachineFrameHelpers@L81881`, `rule.21.Lower-Async-Proc@L81897`, `requirement.21.AsyncFrameInitIRSemantics@L81917`, `rule.21.Lower-Async-Resume@L81936`, `requirement.21.AsyncResumeSwitchIRSemantics@L81955`, `rule.21.Lower-Async-Suspend@L81969`, `rule.21.Lower-Async-Complete@L81988`, `rule.21.Lower-Async-Fail@L82007`
- `requirement.21.AsyncFailStateIRSemantics@L82026`, `diagnostics.21.AsyncStateMachineDiagnostics@L82042`, `requirement.21.AsyncKeySyntaxSurface@L82063`, `requirement.21.AsyncKeyParsingSurface@L82078`, `def.21.AsyncKeyExistingAstForms@L82095`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82111`, `requirement.21.AsyncKeyRestrictions@L82128`, `rule.21.A-Closure-Yield-Keys-Err@L82145`
- `requirement.21.SharedCapturingClosureYieldKeys@L82164`, `requirement.21.YieldReleaseStalenessWarning@L82177`, `requirements.21.AsyncCapabilityRequirements@L82192`, `requirement.21.AsyncSuspensionAccessRights@L82212`, `requirement.21.YieldReleaseRuntimeReference@L82225`, `requirement.21.AsyncKeyFailureHandlingReference@L82238`, `def.21.AsyncKeyIR@L82253`, `rule.21.Lower-Wait-Key-Illegal@L82266`
- `rule.21.Lower-Yield-Release-Keys@L82284`, `rule.21.Lower-YieldFrom-Release-Keys@L82302`, `rule.21.Lower-Closure-Yield-Shared@L82320`, `requirement.21.StaleValueMarkIRDiagnostics@L82338`, `diagnostics.21.AsyncKeyDiagnostics@L82353`, `diagnostics.21.AsyncDiagnosticsSupplement@L82372`
- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77702`, `requirement.21.ReservedAsyncTypeConstructors@L77715`, `requirement.21.AsyncParameterDefaults@L77735`, `requirement.21.ReservedAsyncStates@L77748`, `parse.21.AsyncTypes@L77767`, `parse.21.UnappliedAsyncPath@L77782`, `ast.21.AsyncModalDeclaration@L77797`, `ast.21.AsyncAliases@L77868`
- `ast.21.AsyncCombinatorMembers@L77889`, `def.21.AsyncSigAndBodyReturnType@L77909`, `rule.21.Sub-Async@L77936`, `rule.21.WF-Async@L77957`, `rule.21.WF-Async-ArgCount-Err@L77975`, `rule.21.WF-Async-Arg-WF-Err@L77993`, `rule.21.WF-Async-Path-Err@L78011`, `requirement.21.AsyncFailedUninhabitedForNeverError@L78029`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L78044`, `def.21.AsyncTypeLoweringForms@L78061`, `requirement.21.AsyncNeverErrorLowering@L78092`, `rule.21.Lower-Async-Type@L78105`, `rule.21.Lower-Async-Alias@L78125`, `diagnostics.21.AsyncType@L78145`, `grammar.21.SuspensionForms@L78164`, `parse.21.SuspensionFormsPrimaryExpressions@L78183`
- `rule.21.Parse-Wait-Expr@L78198`, `rule.21.Parse-Yield-From-Expr@L78218`, `rule.21.Parse-Yield-Expr@L78241`, `ast.21.SuspensionForms@L78264`, `ast.21.SuspensionFormResolution@L78284`, `ast.21.SuspensionFormEvaluationOrder@L78303`, `rule.21.T-Wait@L78326`, `rule.21.T-Wait-Future@L78344`
- `rule.21.Wait-Handle-Err@L78362`, `rule.21.T-Yield@L78382`, `rule.21.Yield-NotAsync-Err@L78400`, `rule.21.Yield-Out-Err@L78418`, `rule.21.T-Yield-From@L78438`, `rule.21.YieldFrom-NotAsync-Err@L78456`, `rule.21.YieldFrom-Out-Err@L78474`, `rule.21.YieldFrom-In-Err@L78493`
- `rule.21.YieldFrom-ErrType-Err@L78511`, `requirement.21.SuspensionKeyRestrictionsReference@L78529`, `requirement.21.WaitRuntimeSemantics@L78544`, `def.21.WaitRuntimeHelpers@L78566`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78590`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78608`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78627`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78640`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78658`, `rule.21.EvalSigma-Wait-Ctrl@L78677`, `requirement.21.YieldRuntimeSemantics@L78695`, `def.21.ResumptionHelpers@L78715`, `rule.21.EvalSigma-Yield@L78750`, `rule.21.EvalSigma-Yield-Release@L78769`, `rule.21.EvalSigma-Yield-Resume@L78788`, `requirement.21.YieldFromRuntimeSemantics@L78807`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78827`, `rule.21.EvalSigma-YieldFrom-Completed@L78847`, `rule.21.EvalSigma-YieldFrom-Failed@L78865`, `rule.21.EvalSigma-YieldFrom-Resume@L78883`, `def.21.EvalYieldFromContinueSignature@L78902`, `rule.21.EvalYieldFromContinue-Suspended@L78917`, `rule.21.EvalYieldFromContinue-Completed@L78937`, `rule.21.EvalYieldFromContinue-Failed@L78955`
- `def.21.SuspensionLoweringForms@L78975`, `rule.21.Lower-Wait-Spawned@L78996`, `rule.21.Lower-Wait-Tracked@L79014`, `rule.21.Lower-Yield@L79032`, `rule.21.Lower-Yield-Release@L79050`, `requirement.21.YieldReleaseReacquireLowering@L79068`, `rule.21.Lower-YieldFrom@L79081`, `requirement.21.YieldFromEnterLoweringLoop@L79099`
- `diagnostics.21.SuspensionForms@L79117`, `requirement.21.AsyncIterationSyntax@L79142`, `grammar.21.CompositionForms@L79157`, `requirement.21.AsyncMethodCallSurfaces@L79176`, `requirement.21.UntilMethodCallSurface@L79196`, `parse.21.CompositionPrimaryExpressions@L79211`, `rule.21.Parse-Sync-Expr@L79226`, `rule.21.Parse-Race-Expr@L79246`
- `rule.21.Parse-RaceArms-Cons@L79266`, `rule.21.Parse-RaceArm@L79284`, `rule.21.Parse-RaceArmsTail-End@L79304`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79322`, `rule.21.Parse-RaceArmsTail-Comma@L79340`, `rule.21.Parse-RaceHandler-Yield@L79358`, `rule.21.Parse-RaceHandler-Return@L79376`, `rule.21.Parse-All-Expr@L79396`
- `parse.21.CompositionOrdinarySurfaces@L79414`, `ast.21.CompositionForms@L79429`, `ast.21.AsyncIterationLoopForm@L79451`, `ast.21.CompositionMethodCallForms@L79468`, `ast.21.CompositionResolution@L79483`, `ast.21.CompositionEvaluationOrder@L79512`, `rule.21.T-Loop-Iter-Async@L79538`, `rule.21.Loop-Async-Err@L79560`
- `requirement.21.ManualSteppingRequirement@L79578`, `def.21.SyncYieldContainment@L79593`, `rule.21.Sync-Yield-Err@L79609`, `rule.21.Sync-YieldFrom-Err@L79627`, `rule.21.T-Sync@L79645`, `rule.21.Sync-Async-Context-Err@L79663`, `rule.21.Sync-Out-Err@L79681`, `rule.21.Sync-In-Err@L79700`
- `def.21.RaceMode@L79720`, `rule.21.T-Race@L79738`, `rule.21.T-Race-Stream@L79759`, `rule.21.Race-Arity-Err@L79780`, `rule.21.Race-Handler-Mix-Err@L79798`, `rule.21.Race-Operand-Out-Err@L79816`, `rule.21.Race-Operand-Err@L79835`, `rule.21.Race-Stream-Operand-Err@L79854`
- `rule.21.Race-Handler-Type-Err@L79873`, `rule.21.Race-Stream-Handler-Type-Err@L79894`, `rule.21.T-All@L79917`, `rule.21.All-Out-Err@L79936`, `rule.21.All-In-Err@L79954`, `def.21.UntilType@L79972`, `def.21.AsyncCombinatorTypes@L79989`, `requirement.21.AsyncCombinatorMemberLookup@L80010`
- `rule.21.T-Async-Map@L80025`, `rule.21.T-Async-Filter@L80043`, `rule.21.T-Async-Take@L80061`, `rule.21.T-Async-Fold@L80079`, `rule.21.T-Async-Chain@L80097`, `requirement.21.AsyncIterationRuntimeSemantics@L80117`, `requirement.21.ManualSteppingRuntimeSemantics@L80135`, `requirement.21.SyncRuntimeSemantics@L80148`
- `def.21.SyncStepSignature@L80168`, `rule.21.SyncStep-Suspended@L80183`, `rule.21.EvalSigma-Sync-Suspended@L80201`, `rule.21.EvalSigma-Sync-Completed@L80220`, `rule.21.EvalSigma-Sync-Failed@L80238`, `requirement.21.RaceReturnRuntimeSemantics@L80256`, `requirement.21.RaceStreamingRuntimeSemantics@L80274`, `def.21.RaceSelectionAndState@L80295`
- `rule.21.InitRace@L80325`, `def.21.RaceStepReturnSignature@L80344`, `rule.21.RaceStepReturn-Completed@L80359`, `rule.21.RaceStepReturn-Failed@L80379`, `rule.21.RaceStepReturn-Continue@L80398`, `rule.21.EvalSigma-Race-Return@L80418`, `def.21.RaceStepStreamSignature@L80437`, `rule.21.RaceStepStream-Yield-Initial@L80452`
- `rule.21.RaceStepStream-AllComplete@L80472`, `rule.21.RaceStepStream-Failed@L80490`, `rule.21.EvalSigma-Race-Stream@L80509`, `def.21.CancelAllSignature@L80528`, `rule.21.CancelAll@L80543`, `def.21.RaceStreamSuspensionState@L80563`, `rule.21.RaceStepStream-Yield-Resumable@L80583`, `rule.21.ResumeRaceState-Step@L80603`
- `rule.21.ResumeRaceState-Done@L80622`, `rule.21.EvalSigma-Race-Stream-Resume@L80639`, `requirement.21.StreamingRaceResumptionOrder@L80659`, `requirement.21.AllRuntimeSemantics@L80672`, `def.21.AllStateAndInitSignature@L80692`, `rule.21.InitAll@L80708`, `def.21.AllStepSignature@L80727`, `rule.21.AllStep-Complete@L80742`
- `rule.21.AllStep-Failed@L80761`, `rule.21.AllStep-Resume@L80781`, `def.21.AllLoopSignature@L80801`, `rule.21.AllLoop-AllCompleted@L80816`, `rule.21.AllLoop-Failed@L80834`, `rule.21.AllLoop-Continue@L80852`, `rule.21.EvalSigma-All@L80871`, `requirement.21.UntilRuntimeSemantics@L80889`
- `def.21.AsyncCombinatorRuntimeWrappers@L80906`, `rule.21.EvalSigma-Map-Create@L80929`, `rule.21.EvalSigma-Map-Resume-Yield@L80947`, `rule.21.EvalSigma-Map-Resume-Complete@L80965`, `rule.21.EvalSigma-Map-Resume-Failed@L80983`, `rule.21.EvalSigma-Filter-Create@L81001`, `rule.21.EvalSigma-Filter-Resume-Pass@L81019`, `rule.21.EvalSigma-Filter-Resume-Skip@L81037`
- `rule.21.EvalSigma-Filter-Resume-Complete@L81056`, `rule.21.EvalSigma-Take-Create@L81074`, `rule.21.EvalSigma-Take-Resume-Yield@L81092`, `rule.21.EvalSigma-Take-Resume-Done@L81110`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81128`, `rule.21.EvalSigma-Fold-Create@L81146`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81164`, `rule.21.EvalSigma-Fold-Resume-Complete@L81182`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81200`, `rule.21.EvalSigma-Chain-Create@L81218`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81236`, `rule.21.EvalSigma-Chain-Resume-Chained@L81254`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81272`, `def.21.AsyncComposeIR@L81292`, `rule.21.Lower-Expr-Sync@L81305`, `requirement.21.SyncLoopIRSemantics@L81323`
- `rule.21.Lower-Expr-Race-Return@L81336`, `rule.21.Lower-Expr-Race-Stream@L81354`, `requirement.21.RaceInitIRSemantics@L81372`, `requirement.21.RaceResumeIRSemantics@L81388`, `rule.21.Lower-Expr-All@L81401`, `requirement.21.AllJoinIRSemantics@L81417`, `requirement.21.AsyncCombinatorWrapperLowering@L81430`, `rule.21.Lower-Async-Map@L81443`
- `rule.21.Lower-Async-Filter@L81459`, `rule.21.Lower-Async-Take@L81475`, `rule.21.Lower-Async-Fold@L81491`, `rule.21.Lower-Async-Chain@L81507`, `requirement.21.AsyncWrapperLoweringSemantics@L81523`, `diagnostics.21.AsyncCompositionDiagnostics@L81538`, `requirement.21.AsyncStateMachineSyntaxSurface@L81568`, `def.21.AsyncProcedureDefinition@L81581`
- `requirement.21.AsyncStateMachineParsingSurface@L81596`, `def.21.AsyncStateMachineHelperForms@L81613`, `requirement.21.AsyncFrameStoredState@L81642`, `def.21.LiveAcrossSuspension@L81661`, `rule.21.Warn-Async-LargeCapture@L81676`, `rule.21.Warn-Async-LargeCapture-Ok@L81694`, `requirement.21.AsyncLargeCaptureWarningEmission@L81712`, `rule.21.Async-Capture-Err@L81725`
- `rule.21.P-Async-Create@L81743`, `rule.21.Prov-Async-Escape-Err@L81762`, `requirement.21.AsyncErrorPropagationTypingReference@L81781`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81796`, `requirement.21.AsyncSettlementRuntimeSemantics@L81814`, `requirement.21.AsyncResumeRuntimeSemantics@L81831`, `requirement.21.AsyncFailureRuntimeSemantics@L81847`, `def.21.AsyncStateMachineLoweringJudgements@L81867`
- `def.21.AsyncStateMachineFrameHelpers@L81881`, `rule.21.Lower-Async-Proc@L81897`, `requirement.21.AsyncFrameInitIRSemantics@L81917`, `rule.21.Lower-Async-Resume@L81936`, `requirement.21.AsyncResumeSwitchIRSemantics@L81955`, `rule.21.Lower-Async-Suspend@L81969`, `rule.21.Lower-Async-Complete@L81988`, `rule.21.Lower-Async-Fail@L82007`
- `requirement.21.AsyncFailStateIRSemantics@L82026`, `diagnostics.21.AsyncStateMachineDiagnostics@L82042`, `requirement.21.AsyncKeySyntaxSurface@L82063`, `requirement.21.AsyncKeyParsingSurface@L82078`, `def.21.AsyncKeyExistingAstForms@L82095`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82111`, `requirement.21.AsyncKeyRestrictions@L82128`, `rule.21.A-Closure-Yield-Keys-Err@L82145`
- `requirement.21.SharedCapturingClosureYieldKeys@L82164`, `requirement.21.YieldReleaseStalenessWarning@L82177`, `requirements.21.AsyncCapabilityRequirements@L82192`, `requirement.21.AsyncSuspensionAccessRights@L82212`, `requirement.21.YieldReleaseRuntimeReference@L82225`, `requirement.21.AsyncKeyFailureHandlingReference@L82238`, `def.21.AsyncKeyIR@L82253`, `rule.21.Lower-Wait-Key-Illegal@L82266`
- `rule.21.Lower-Yield-Release-Keys@L82284`, `rule.21.Lower-YieldFrom-Release-Keys@L82302`, `rule.21.Lower-Closure-Yield-Shared@L82320`, `requirement.21.StaleValueMarkIRDiagnostics@L82338`, `diagnostics.21.AsyncKeyDiagnostics@L82353`, `diagnostics.21.AsyncDiagnosticsSupplement@L82372`

#### `spec.comptime`

Count: 181 total; 181 required; 0 recommended; 0 informative. Ledger line span: L82027-L85037.

- `requirement.22.Phase2ExecutionPosition@L82392`, `grammar.22.CompileTimeForms@L82409`, `def.22.CtParseJudg@L82431`, `rule.22.Parse-CtProc@L82444`, `rule.22.Parse-CtStmt@L82460`, `rule.22.Parse-CtExpr@L82476`, `rule.22.Parse-CtIf@L82492`, `rule.22.Parse-CtLoopIter@L82508`
- `rule.22.Parse-CtElseOpt-None@L82524`, `rule.22.Parse-CtElseOpt-Block@L82540`, `rule.22.Parse-CtElseOpt-ElseIf@L82556`, `def.22.CtNodeForms@L82574`, `def.22.CtExecutionState@L82593`, `def.22.CompileTimeJudgementSets@L82621`, `def.22.CtValueForms@L82640`, `def.22.CompileTimeTypingEnvironment@L82661`
- `def.22.CtAvailabilityAndForbiddenTypes@L82674`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82707`, `requirement.22.CompileTimeProhibitedConstructs@L82720`, `rule.22.T-CtStmt@L82738`, `rule.22.T-CtExpr@L82754`, `rule.22.T-CtIf@L82770`, `rule.22.T-CtLoopIter@L82786`, `rule.22.T-CtProc@L82802`
- `requirement.22.CompileTimeProcedureContracts@L82818`, `requirement.22.CompileTimeProcedureContextRestriction@L82831`, `requirement.22.ComptimeIfSelectedBranchOnly@L82844`, `requirement.22.ComptimeLoopIterationSemantics@L82857`, `def.22.Phase2ModuleOrder@L82873`, `def.22.CtDynamicHelpers@L82886`, `requirement.22.ComptimePassExecutionRequirements@L82908`, `requirement.22.CtEvalOrdinarySemantics@L82927`
- `requirement.22.CtExpandOrdinaryTraversal@L82940`, `rule.22.ComptimePass-Empty@L82953`, `rule.22.ComptimePass-Cons@L82968`, `rule.22.ComptimePass@L82984`, `rule.22.CtExecModule@L83002`, `rule.22.CtExpandItemSeq-Empty@L83018`, `rule.22.CtExpandItemSeq-Cons@L83033`, `def.22.CtExpandItemResult@L83049`
- `requirement.22.CtPendingEmitsTransfer@L83062`, `rule.22.CtExpandItem-CtProc@L83075`, `rule.22.CtExpandStmtSeq-Empty@L83091`, `rule.22.CtExpandStmtSeq-Cons@L83106`, `rule.22.CtExpandBlock@L83122`, `rule.22.CtExpandStmt-CtStmt@L83138`, `rule.22.CtExpandExpr-CtExpr@L83154`, `rule.22.CtExpandExpr-CtIf-True@L83170`
- `rule.22.CtExpandExpr-CtIf-False@L83186`, `rule.22.CtExpandExpr-CtLoopIter@L83202`, `rule.22.CtLoopIterUnroll-Empty@L83218`, `rule.22.CtLoopIterUnroll-Cons@L83233`, `def.22.CtLiteralize@L83249`, `requirement.22.CompileTimeFormsLowering@L83273`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83293`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83310`
- `def.22.CtCapName@L83325`, `rule.22.Parse-CtCapRef@L83338`, `requirement.22.CtCapMethodCallParsing@L83354`, `def.22.CtCapabilitiesAndBuiltinTypes@L83369`, `def.22.CtReflectionInfoFields@L83392`, `def.22.CtValueConversionHelpers@L83408`, `def.22.TypeEmitterInterface@L83438`, `def.22.IntrospectInterface@L83454`
- `def.22.ProjectFilesInterface@L83476`, `def.22.ComptimeDiagnosticsInterface@L83496`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83518`, `requirement.22.TypeEmitterAvailability@L83531`, `requirement.22.ProjectFilesAvailability@L83546`, `def.22.CtCapBindings@L83559`, `requirement.22.ProjectFilesPathRestrictions@L83572`, `requirement.22.TypeEmitterEmitTypeRequirement@L83589`
- `def.22.CtCapabilityDynamicHelpers@L83604`, `rule.22.CtBuiltin-Emit@L83627`, `rule.22.CtBuiltin-ProjectRoot@L83643`, `rule.22.CtBuiltin-Read@L83659`, `rule.22.CtBuiltin-Read-InvalidPath@L83675`, `rule.22.CtBuiltin-ReadBytes@L83691`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83707`, `rule.22.CtBuiltin-Exists@L83723`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83739`, `rule.22.CtBuiltin-ListDir@L83755`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83771`, `rule.22.CtBuiltin-Diagnostics-Error@L83787`, `rule.22.CtBuiltin-Diagnostics-Warning@L83803`, `rule.22.CtBuiltin-Diagnostics-Note@L83819`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83835`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83851`
- `requirement.22.ProjectFileSnapshotStability@L83867`, `requirement.22.CompileTimeCapabilitiesLowering@L83882`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83897`, `grammar.22.TypeLiteral@L83914`, `def.22.ReflectParseJudg@L83931`, `rule.22.Parse-TypeLiteral@L83944`, `def.22.Reflectable@L83962`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L83987`
- `def.22.TypeCategory@L84001`, `def.22.ReflectFields@L84042`, `def.22.ReflectVariants@L84059`, `def.22.ReflectStates@L84076`, `def.22.ReflectionPayloadAndModuleHelpers@L84094`, `rule.22.T-TypeLiteral@L84119`, `requirement.22.IntrospectCategoryValidity@L84135`, `requirement.22.IntrospectMemberValidity@L84148`
- `requirement.22.ReflectionCanonicalOrder@L84163`, `requirement.22.IntrospectImplementsFormSemantics@L84179`, `rule.22.CtEval-TypeLiteral@L84194`, `rule.22.CtBuiltin-Reflect-Category@L84210`, `rule.22.CtBuiltin-Reflect-Fields@L84226`, `rule.22.CtBuiltin-Reflect-Variants@L84242`, `rule.22.CtBuiltin-Reflect-States@L84258`, `rule.22.CtBuiltin-Reflect-Form@L84274`
- `rule.22.CtBuiltin-Reflect-TypeName@L84290`, `rule.22.CtBuiltin-Reflect-ModulePath@L84306`, `requirement.22.ReflectionPurityAndImmutability@L84322`, `requirement.22.ReflectionLowering@L84337`, `diagnostics.22.ReflectionDiagnosticsReference@L84352`, `grammar.22.QuoteSpliceEmission@L84369`, `def.22.QuoteParseJudg@L84391`, `def.22.CaptureQuotedTokens@L84404`
- `rule.22.Parse-Quote-Raw@L84417`, `rule.22.Parse-Quote-Type@L84433`, `rule.22.Parse-Quote-Pattern@L84449`, `def.22.AstForms@L84467`, `def.22.QuoteSpliceHygieneForms@L84486`, `def.22.QuoteJudg@L84503`, `def.22.ExpectedAstKind@L84516`, `def.22.CtLiteralType@L84534`
- `def.22.SpliceCompat@L84555`, `requirement.22.QuoteCompileTimeOnly@L84575`, `def.22.ResolveQuoteKind@L84588`, `requirement.22.QuotedContentValidity@L84603`, `requirement.22.SpliceContextAndTypeCompatibility@L84616`, `requirement.22.SpliceIdentifierPositionRestrictions@L84629`, `requirement.22.StringSpliceIdentifierHygiene@L84642`, `requirement.22.EmitterEmitWellFormedness@L84655`
- `def.22.ParseQuotedBody@L84670`, `def.22.RenderSplice@L84687`, `requirement.22.HygienizeAstProperties@L84705`, `requirement.22.HygienicInternalReferences@L84721`, `requirement.22.ImportUsingHygiene@L84734`, `rule.22.CtEval-Quote@L84747`, `requirement.22.QuoteBuildSpliceOrder@L84763`, `requirement.22.EmissionOrder@L84777`
- `requirement.22.QuoteSpliceEmissionLowering@L84794`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84809`, `grammar.22.DeriveTargetsAndContracts@L84826`, `def.22.DeriveParseJudg@L84847`, `requirement.22.DeriveAttributeParsingReference@L84860`, `rule.22.Parse-DeriveTargetDecl@L84873`, `rule.22.Parse-DeriveContractOpt-None@L84889`, `rule.22.Parse-DeriveContractOpt-Yes@L84905`
- `rule.22.Parse-DeriveClauseList-Cons@L84921`, `rule.22.Parse-DeriveClause-Requires@L84937`, `rule.22.Parse-DeriveClause-Emits@L84953`, `rule.22.Parse-DeriveClauseTail-End@L84969`, `rule.22.Parse-DeriveClauseTail-Comma@L84985`, `def.22.DeriveTargetDecl@L85003`, `def.22.DeriveGraphAndOrder@L85018`, `requirement.22.DeriveAttributeTargetKinds@L85042`
- `requirement.22.DeriveTargetNameResolution@L85055`, `requirement.22.DeriveTargetBodyBindings@L85068`, `requirement.22.DeriveTargetBodyRestrictions@L85085`, `requirement.22.DeriveExecutionOrder@L85098`, `requirement.22.DeriveOrderTieBreaker@L85113`, `requirement.22.DeriveRequiresValidation@L85126`, `requirement.22.DeriveEmitsValidation@L85139`, `requirement.22.DeriveRequiresEmitsScope@L85152`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85167`, `rule.22.CtExpandItem-DeriveTargetDecl@L85180`, `rule.22.RunDeriveSet-Empty@L85196`, `rule.22.RunDeriveSet-Cons@L85211`, `rule.22.RunDeriveTarget@L85227`, `def.22.BindDeriveTargetInputs@L85243`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85256`, `requirement.22.DeriveTargetExecutionTiming@L85272`
- `requirement.22.DeriveTargetFailureSemantics@L85285`, `requirement.22.DeriveTargetsLowering@L85300`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85315`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85330`, `requirement.22.UserDiagnosticBuiltinEmission@L85406`
- `requirement.22.Phase2ExecutionPosition@L82392`, `grammar.22.CompileTimeForms@L82409`, `def.22.CtParseJudg@L82431`, `rule.22.Parse-CtProc@L82444`, `rule.22.Parse-CtStmt@L82460`, `rule.22.Parse-CtExpr@L82476`, `rule.22.Parse-CtIf@L82492`, `rule.22.Parse-CtLoopIter@L82508`
- `rule.22.Parse-CtElseOpt-None@L82524`, `rule.22.Parse-CtElseOpt-Block@L82540`, `rule.22.Parse-CtElseOpt-ElseIf@L82556`, `def.22.CtNodeForms@L82574`, `def.22.CtExecutionState@L82593`, `def.22.CompileTimeJudgementSets@L82621`, `def.22.CtValueForms@L82640`, `def.22.CompileTimeTypingEnvironment@L82661`
- `def.22.CtAvailabilityAndForbiddenTypes@L82674`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82707`, `requirement.22.CompileTimeProhibitedConstructs@L82720`, `rule.22.T-CtStmt@L82738`, `rule.22.T-CtExpr@L82754`, `rule.22.T-CtIf@L82770`, `rule.22.T-CtLoopIter@L82786`, `rule.22.T-CtProc@L82802`
- `requirement.22.CompileTimeProcedureContracts@L82818`, `requirement.22.CompileTimeProcedureContextRestriction@L82831`, `requirement.22.ComptimeIfSelectedBranchOnly@L82844`, `requirement.22.ComptimeLoopIterationSemantics@L82857`, `def.22.Phase2ModuleOrder@L82873`, `def.22.CtDynamicHelpers@L82886`, `requirement.22.ComptimePassExecutionRequirements@L82908`, `requirement.22.CtEvalOrdinarySemantics@L82927`
- `requirement.22.CtExpandOrdinaryTraversal@L82940`, `rule.22.ComptimePass-Empty@L82953`, `rule.22.ComptimePass-Cons@L82968`, `rule.22.ComptimePass@L82984`, `rule.22.CtExecModule@L83002`, `rule.22.CtExpandItemSeq-Empty@L83018`, `rule.22.CtExpandItemSeq-Cons@L83033`, `def.22.CtExpandItemResult@L83049`
- `requirement.22.CtPendingEmitsTransfer@L83062`, `rule.22.CtExpandItem-CtProc@L83075`, `rule.22.CtExpandStmtSeq-Empty@L83091`, `rule.22.CtExpandStmtSeq-Cons@L83106`, `rule.22.CtExpandBlock@L83122`, `rule.22.CtExpandStmt-CtStmt@L83138`, `rule.22.CtExpandExpr-CtExpr@L83154`, `rule.22.CtExpandExpr-CtIf-True@L83170`
- `rule.22.CtExpandExpr-CtIf-False@L83186`, `rule.22.CtExpandExpr-CtLoopIter@L83202`, `rule.22.CtLoopIterUnroll-Empty@L83218`, `rule.22.CtLoopIterUnroll-Cons@L83233`, `def.22.CtLiteralize@L83249`, `requirement.22.CompileTimeFormsLowering@L83273`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83293`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83310`
- `def.22.CtCapName@L83325`, `rule.22.Parse-CtCapRef@L83338`, `requirement.22.CtCapMethodCallParsing@L83354`, `def.22.CtCapabilitiesAndBuiltinTypes@L83369`, `def.22.CtReflectionInfoFields@L83392`, `def.22.CtValueConversionHelpers@L83408`, `def.22.TypeEmitterInterface@L83438`, `def.22.IntrospectInterface@L83454`
- `def.22.ProjectFilesInterface@L83476`, `def.22.ComptimeDiagnosticsInterface@L83496`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83518`, `requirement.22.TypeEmitterAvailability@L83531`, `requirement.22.ProjectFilesAvailability@L83546`, `def.22.CtCapBindings@L83559`, `requirement.22.ProjectFilesPathRestrictions@L83572`, `requirement.22.TypeEmitterEmitTypeRequirement@L83589`
- `def.22.CtCapabilityDynamicHelpers@L83604`, `rule.22.CtBuiltin-Emit@L83627`, `rule.22.CtBuiltin-ProjectRoot@L83643`, `rule.22.CtBuiltin-Read@L83659`, `rule.22.CtBuiltin-Read-InvalidPath@L83675`, `rule.22.CtBuiltin-ReadBytes@L83691`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83707`, `rule.22.CtBuiltin-Exists@L83723`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83739`, `rule.22.CtBuiltin-ListDir@L83755`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83771`, `rule.22.CtBuiltin-Diagnostics-Error@L83787`, `rule.22.CtBuiltin-Diagnostics-Warning@L83803`, `rule.22.CtBuiltin-Diagnostics-Note@L83819`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83835`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83851`
- `requirement.22.ProjectFileSnapshotStability@L83867`, `requirement.22.CompileTimeCapabilitiesLowering@L83882`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83897`, `grammar.22.TypeLiteral@L83914`, `def.22.ReflectParseJudg@L83931`, `rule.22.Parse-TypeLiteral@L83944`, `def.22.Reflectable@L83962`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L83987`
- `def.22.TypeCategory@L84001`, `def.22.ReflectFields@L84042`, `def.22.ReflectVariants@L84059`, `def.22.ReflectStates@L84076`, `def.22.ReflectionPayloadAndModuleHelpers@L84094`, `rule.22.T-TypeLiteral@L84119`, `requirement.22.IntrospectCategoryValidity@L84135`, `requirement.22.IntrospectMemberValidity@L84148`
- `requirement.22.ReflectionCanonicalOrder@L84163`, `requirement.22.IntrospectImplementsFormSemantics@L84179`, `rule.22.CtEval-TypeLiteral@L84194`, `rule.22.CtBuiltin-Reflect-Category@L84210`, `rule.22.CtBuiltin-Reflect-Fields@L84226`, `rule.22.CtBuiltin-Reflect-Variants@L84242`, `rule.22.CtBuiltin-Reflect-States@L84258`, `rule.22.CtBuiltin-Reflect-Form@L84274`
- `rule.22.CtBuiltin-Reflect-TypeName@L84290`, `rule.22.CtBuiltin-Reflect-ModulePath@L84306`, `requirement.22.ReflectionPurityAndImmutability@L84322`, `requirement.22.ReflectionLowering@L84337`, `diagnostics.22.ReflectionDiagnosticsReference@L84352`, `grammar.22.QuoteSpliceEmission@L84369`, `def.22.QuoteParseJudg@L84391`, `def.22.CaptureQuotedTokens@L84404`
- `rule.22.Parse-Quote-Raw@L84417`, `rule.22.Parse-Quote-Type@L84433`, `rule.22.Parse-Quote-Pattern@L84449`, `def.22.AstForms@L84467`, `def.22.QuoteSpliceHygieneForms@L84486`, `def.22.QuoteJudg@L84503`, `def.22.ExpectedAstKind@L84516`, `def.22.CtLiteralType@L84534`
- `def.22.SpliceCompat@L84555`, `requirement.22.QuoteCompileTimeOnly@L84575`, `def.22.ResolveQuoteKind@L84588`, `requirement.22.QuotedContentValidity@L84603`, `requirement.22.SpliceContextAndTypeCompatibility@L84616`, `requirement.22.SpliceIdentifierPositionRestrictions@L84629`, `requirement.22.StringSpliceIdentifierHygiene@L84642`, `requirement.22.EmitterEmitWellFormedness@L84655`
- `def.22.ParseQuotedBody@L84670`, `def.22.RenderSplice@L84687`, `requirement.22.HygienizeAstProperties@L84705`, `requirement.22.HygienicInternalReferences@L84721`, `requirement.22.ImportUsingHygiene@L84734`, `rule.22.CtEval-Quote@L84747`, `requirement.22.QuoteBuildSpliceOrder@L84763`, `requirement.22.EmissionOrder@L84777`
- `requirement.22.QuoteSpliceEmissionLowering@L84794`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84809`, `grammar.22.DeriveTargetsAndContracts@L84826`, `def.22.DeriveParseJudg@L84847`, `requirement.22.DeriveAttributeParsingReference@L84860`, `rule.22.Parse-DeriveTargetDecl@L84873`, `rule.22.Parse-DeriveContractOpt-None@L84889`, `rule.22.Parse-DeriveContractOpt-Yes@L84905`
- `rule.22.Parse-DeriveClauseList-Cons@L84921`, `rule.22.Parse-DeriveClause-Requires@L84937`, `rule.22.Parse-DeriveClause-Emits@L84953`, `rule.22.Parse-DeriveClauseTail-End@L84969`, `rule.22.Parse-DeriveClauseTail-Comma@L84985`, `def.22.DeriveTargetDecl@L85003`, `def.22.DeriveGraphAndOrder@L85018`, `requirement.22.DeriveAttributeTargetKinds@L85042`
- `requirement.22.DeriveTargetNameResolution@L85055`, `requirement.22.DeriveTargetBodyBindings@L85068`, `requirement.22.DeriveTargetBodyRestrictions@L85085`, `requirement.22.DeriveExecutionOrder@L85098`, `requirement.22.DeriveOrderTieBreaker@L85113`, `requirement.22.DeriveRequiresValidation@L85126`, `requirement.22.DeriveEmitsValidation@L85139`, `requirement.22.DeriveRequiresEmitsScope@L85152`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85167`, `rule.22.CtExpandItem-DeriveTargetDecl@L85180`, `rule.22.RunDeriveSet-Empty@L85196`, `rule.22.RunDeriveSet-Cons@L85211`, `rule.22.RunDeriveTarget@L85227`, `def.22.BindDeriveTargetInputs@L85243`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85256`, `requirement.22.DeriveTargetExecutionTiming@L85272`
- `requirement.22.DeriveTargetFailureSemantics@L85285`, `requirement.22.DeriveTargetsLowering@L85300`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85315`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85330`, `requirement.22.UserDiagnosticBuiltinEmission@L85406`

#### `spec.ffi`

Count: 203 total; 203 required; 0 recommended; 0 informative. Ledger line span: L85054-L88478.

- `requirement.23.FFIBoundaryDefinition@L85423`, `def.23.FFIBoundary@L85436`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85453`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85468`, `def.23.FfiSafeTypePredicateAstForm@L85483`, `def.23.FfiSafePredicateMeaning@L85498`, `def.23.FfiSafeJudgements@L85511`, `def.23.FfiPrimitiveTypes@L85524`
- `def.23.FfiLayoutAndPayloadHelpers@L85537`, `def.23.FfiTypeParameterSetHelper@L85553`, `def.23.FfiAliasHelpers@L85566`, `def.23.TypeSubst@L85580`, `def.23.TypeParamsIn@L85617`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85655`, `def.23.FfiSafePredicateClauseHelpers@L85669`, `def.23.ProhibitedFfiType@L85683`
- `def.23.FfiByValueHelpers@L85714`, `rule.23.FfiSafe-Prim@L85729`, `rule.23.FfiSafe-RawPtr@L85745`, `rule.23.FfiSafe-Array@L85761`, `rule.23.FfiSafe-Func@L85777`, `rule.23.FfiSafe-Perm@L85793`, `rule.23.FfiSafe-Alias@L85809`, `rule.23.FfiSafe-Alias-Apply@L85825`
- `rule.23.FfiSafe-Record@L85841`, `rule.23.FfiSafe-Record-Apply@L85857`, `rule.23.FfiSafe-Enum@L85873`, `rule.23.FfiSafe-Enum-Apply@L85889`, `rule.23.FfiSafe-Prohibited-Err@L85905`, `rule.23.FfiSafe-Record-LayoutC-Err@L85921`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85937`, `rule.23.FfiSafe-Record-Field-Err@L85953`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85969`, `rule.23.FfiSafe-Enum-Field-Err@L85985`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L86001`, `rule.23.FfiSafe-Incomplete-Err@L86017`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L86033`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L86049`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L86065`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L86081`
- `requirement.23.FfiSafeProhibitedCategories@L86097`, `requirement.23.FfiSafeRaiiByValueRule@L86124`, `requirement.23.FfiSafeGenericBounds@L86137`, `requirement.23.FfiSafeDynamicSemantics@L86152`, `requirement.23.FfiSafeLowering@L86167`, `diagnostics.23.FfiSafeDiagnostics@L86182`, `grammar.23.ExternProcedureDecl@L86208`, `rule.23.Parse-ExternProcDecl@L86225`
- `ast.23.ExternProcDeclForm@L86243`, `def.23.ExternProcedureDerivedForms@L86260`, `def.23.ExternProcedureMeaning@L86280`, `def.23.ExternAbiStrings@L86293`, `def.23.ExternSignatureRequirements@L86315`, `requirement.23.ExternFfiConstraints@L86338`, `requirement.23.ExternCallSafety@L86355`, `requirement.23.ExternDynamicSemantics@L86372`
- `requirement.23.ExternLowering@L86387`, `diagnostics.23.ExternProcedureDiagnostics@L86402`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86418`, `requirement.23.RawExportedProcedureClassification@L86435`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86450`, `requirement.23.RawExportParsingClassification@L86463`, `ast.23.RawExportProcedureForm@L86478`, `def.23.RawExportedProcedureMeaning@L86495`
- `def.23.ZeroValueHelpers@L86508`, `def.23.ExportSignatureHelpers@L86525`, `rule.23.ExportSig-Ok@L86539`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86557`, `requirement.23.RawExportLibraryImageLifecycle@L86570`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86583`, `requirement.23.RawExportLowering@L86598`, `diagnostics.23.RawExportDiagnostics@L86613`
- `diagnostics.23.RawExportDiagnosticOwnership@L86629`, `requirement.23.HostedExportClassification@L86644`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86659`, `requirement.23.HostedExportParsingClassification@L86672`, `ast.23.HostedExportProcedureForm@L86687`, `def.23.HostedExportProcedureHelpers@L86700`, `requirement.23.HostedRootCapsMeaning@L86727`, `def.23.HostedExportMeaning@L86742`
- `requirement.23.HostedExportForeignVisibleSignature@L86755`, `requirement.23.HostedExportForeignVisiblePassKind@L86768`, `def.23.HostExportSignatureJudgements@L86781`, `rule.23.HostExportSig-Ok@L86794`, `rule.23.HostExport-Library-Err@L86810`, `rule.23.HostExport-MixedMode-Err@L86826`, `rule.23.HostExport-Generic-Err@L86842`, `rule.23.HostExport-Context-Err@L86858`
- `rule.23.HostExport-Context-Raw-Err@L86874`, `rule.23.HostExport-Context-Move-Err@L86890`, `requirement.23.HostedExportSessionHandleValidity@L86908`, `requirement.23.HostedExportCapabilityIsolation@L86921`, `requirement.23.HostedSessionRootCapsGrant@L86934`, `requirement.23.HostedExportBoundaryEntrySequence@L86947`, `requirement.23.HostedExportInvalidHandleBehavior@L86966`, `requirement.23.HostedExportCatchFailureReturn@L86982`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L86997`, `requirement.23.HostedExportThunkAbiDetermination@L87010`, `def.23.HostThunkCarrierHelpers@L87028`, `rule.23.HostThunkParamCarrier-ByRef@L87056`, `rule.23.HostThunkParamCarrier-ByValue-Default@L87072`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87088`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87104`, `rule.23.HostThunkRetCarrier-Default@L87120`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87136`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87152`, `requirement.23.HostedExportThunkShapeUse@L87168`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87181`, `requirement.23.HostedExportNoExtraAbiRewriting@L87194`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87207`, `requirement.23.HostedThunkToSourceCallReconstruction@L87220`, `requirement.23.HostedStateSymbolResolution@L87233`
- `requirement.23.HostedLibraryLifecycleExports@L87246`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87263`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87276`, `requirement.23.HostedSessionHandleNoReissue@L87289`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87302`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87321`, `diagnostics.23.HostedExportDiagnostics@L87336`, `diagnostics.23.HostedExportDiagnosticOwnership@L87355`
- `grammar.23.FfiAttributes@L87372`, `requirement.23.FfiAttributesParsing@L87401`, `ast.23.FfiAttributesAttachedEntries@L87416`, `ast.23.FfiAttributeTargets@L87429`, `requirement.23.MangleAttributeSemantics@L87453`, `def.23.LibraryLinkKinds@L87472`, `requirement.23.LibraryAttributeSemantics@L87492`, `def.23.ResolveLibraryName@L87509`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87536`, `requirement.23.RawDylibResolution@L87549`, `def.23.UnwindModes@L87567`, `requirement.23.UnwindDefaultMode@L87585`, `requirement.23.UnwindAttributeTargetValidity@L87598`, `requirement.23.UnwindCatchAbiRequirement@L87611`, `requirement.23.ExportAttributeSemantics@L87631`, `requirement.23.HostExportAttributeSemantics@L87651`
- `requirement.23.FfiPassByValueAttributeSemantics@L87673`, `requirement.23.FfiAttributeConstraints@L87686`, `requirement.23.FfiAttributesDynamicSemantics@L87712`, `requirement.23.FfiAttributesLowering@L87727`, `diagnostics.23.FfiAttributeDiagnostics@L87742`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87775`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87790`, `ast.23.CapabilityIsolationNoDedicatedAst@L87805`
- `requirement.23.CapabilityIsolationSemantics@L87820`, `def.23.CapabilityIsolationHelpers@L87836`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87853`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87871`, `requirement.23.CapabilityIsolationDynamicSemantics@L87891`, `requirement.23.CapabilityIsolationLowering@L87906`, `diagnostics.23.CapabilityIsolationDiagnostics@L87921`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87936`
- `grammar.23.ForeignContracts@L87953`, `def.23.ForeignContractStart@L87978`, `rule.23.Parse-ForeignContractClauseListOpt-None@L87991`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L88007`, `rule.23.Parse-ForeignContractClauseList-Cons@L88023`, `rule.23.Parse-ForeignContractClauseListTail-End@L88039`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L88055`, `rule.23.Parse-ForeignContractClause-Assumes@L88071`
- `rule.23.Parse-ForeignContractClause-Ensures@L88087`, `def.23.ForeignEnsuresKindAndExpr@L88103`, `rule.23.Parse-EnsuresPredicate-Error@L88122`, `rule.23.Parse-EnsuresPredicate-NullResult@L88138`, `rule.23.Parse-EnsuresPredicate-Plain@L88154`, `ast.23.ForeignContractsForm@L88171`, `ast.23.EnsuresPredicateForms@L88194`, `def.23.ForeignPreconditions@L88216`
- `requirement.23.ForeignPredicateContext@L88229`, `def.23.ForeignPreconditionVerificationModes@L88255`, `requirement.23.ForeignPreconditionVerificationLowering@L88273`, `def.23.ForeignPostconditions@L88288`, `requirement.23.ForeignPostconditionPredicateBindings@L88301`, `def.23.ForeignPostconditionClassification@L88321`, `requirement.23.NullResultWellFormedness@L88354`, `def.23.NullableFfiResult@L88370`
- `rule.23.ForeignEnsures-NullResult-Err@L88386`, `requirement.23.ErrorPredicateWellFormedness@L88405`, `def.23.ForeignPostconditionVerificationModes@L88418`, `requirement.23.ForeignPostconditionStaticVerification@L88436`, `def.23.ForeignContractVerificationSummary@L88451`, `requirement.23.ForeignPreconditionDynamicFailure@L88471`, `requirement.23.ForeignPostconditionDynamicChecks@L88484`, `requirement.23.ForeignContractsLowering@L88499`
- `diagnostics.23.ForeignContractDiagnostics@L88514`, `requirement.23.BoundaryUnwindingSyntax@L88541`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88556`, `ast.23.BoundaryUnwindPolicySource@L88571`, `def.23.UnwindModeAstHelpers@L88584`, `def.23.DetermineUnwindMode@L88606`, `def.23.ParseUnwindArg@L88631`, `rule.23.UnwindMode-Valid@L88653`
- `rule.23.UnwindMode-Invalid-Err@L88671`, `requirement.23.BoundaryUnwindDynamicEffects@L88691`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88710`, `def.23.BoundaryUnwindCodeGenerationEffects@L88725`, `rule.23.CodeGen-UnwindAbort-Import@L88745`, `rule.23.CodeGen-UnwindCatch-Import@L88763`, `rule.23.CodeGen-UnwindAbort-Export@L88781`, `rule.23.CodeGen-UnwindCatch-Export@L88799`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88819`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88832`, `diagnostics.23.FfiDiagnosticsSupplement@L88847`
- `requirement.23.FFIBoundaryDefinition@L85423`, `def.23.FFIBoundary@L85436`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85453`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85468`, `def.23.FfiSafeTypePredicateAstForm@L85483`, `def.23.FfiSafePredicateMeaning@L85498`, `def.23.FfiSafeJudgements@L85511`, `def.23.FfiPrimitiveTypes@L85524`
- `def.23.FfiLayoutAndPayloadHelpers@L85537`, `def.23.FfiTypeParameterSetHelper@L85553`, `def.23.FfiAliasHelpers@L85566`, `def.23.TypeSubst@L85580`, `def.23.TypeParamsIn@L85617`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85655`, `def.23.FfiSafePredicateClauseHelpers@L85669`, `def.23.ProhibitedFfiType@L85683`
- `def.23.FfiByValueHelpers@L85714`, `rule.23.FfiSafe-Prim@L85729`, `rule.23.FfiSafe-RawPtr@L85745`, `rule.23.FfiSafe-Array@L85761`, `rule.23.FfiSafe-Func@L85777`, `rule.23.FfiSafe-Perm@L85793`, `rule.23.FfiSafe-Alias@L85809`, `rule.23.FfiSafe-Alias-Apply@L85825`
- `rule.23.FfiSafe-Record@L85841`, `rule.23.FfiSafe-Record-Apply@L85857`, `rule.23.FfiSafe-Enum@L85873`, `rule.23.FfiSafe-Enum-Apply@L85889`, `rule.23.FfiSafe-Prohibited-Err@L85905`, `rule.23.FfiSafe-Record-LayoutC-Err@L85921`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85937`, `rule.23.FfiSafe-Record-Field-Err@L85953`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85969`, `rule.23.FfiSafe-Enum-Field-Err@L85985`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L86001`, `rule.23.FfiSafe-Incomplete-Err@L86017`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L86033`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L86049`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L86065`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L86081`
- `requirement.23.FfiSafeProhibitedCategories@L86097`, `requirement.23.FfiSafeRaiiByValueRule@L86124`, `requirement.23.FfiSafeGenericBounds@L86137`, `requirement.23.FfiSafeDynamicSemantics@L86152`, `requirement.23.FfiSafeLowering@L86167`, `diagnostics.23.FfiSafeDiagnostics@L86182`, `grammar.23.ExternProcedureDecl@L86208`, `rule.23.Parse-ExternProcDecl@L86225`
- `ast.23.ExternProcDeclForm@L86243`, `def.23.ExternProcedureDerivedForms@L86260`, `def.23.ExternProcedureMeaning@L86280`, `def.23.ExternAbiStrings@L86293`, `def.23.ExternSignatureRequirements@L86315`, `requirement.23.ExternFfiConstraints@L86338`, `requirement.23.ExternCallSafety@L86355`, `requirement.23.ExternDynamicSemantics@L86372`
- `requirement.23.ExternLowering@L86387`, `diagnostics.23.ExternProcedureDiagnostics@L86402`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86418`, `requirement.23.RawExportedProcedureClassification@L86435`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86450`, `requirement.23.RawExportParsingClassification@L86463`, `ast.23.RawExportProcedureForm@L86478`, `def.23.RawExportedProcedureMeaning@L86495`
- `def.23.ZeroValueHelpers@L86508`, `def.23.ExportSignatureHelpers@L86525`, `rule.23.ExportSig-Ok@L86539`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86557`, `requirement.23.RawExportLibraryImageLifecycle@L86570`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86583`, `requirement.23.RawExportLowering@L86598`, `diagnostics.23.RawExportDiagnostics@L86613`
- `diagnostics.23.RawExportDiagnosticOwnership@L86629`, `requirement.23.HostedExportClassification@L86644`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86659`, `requirement.23.HostedExportParsingClassification@L86672`, `ast.23.HostedExportProcedureForm@L86687`, `def.23.HostedExportProcedureHelpers@L86700`, `requirement.23.HostedRootCapsMeaning@L86727`, `def.23.HostedExportMeaning@L86742`
- `requirement.23.HostedExportForeignVisibleSignature@L86755`, `requirement.23.HostedExportForeignVisiblePassKind@L86768`, `def.23.HostExportSignatureJudgements@L86781`, `rule.23.HostExportSig-Ok@L86794`, `rule.23.HostExport-Library-Err@L86810`, `rule.23.HostExport-MixedMode-Err@L86826`, `rule.23.HostExport-Generic-Err@L86842`, `rule.23.HostExport-Context-Err@L86858`
- `rule.23.HostExport-Context-Raw-Err@L86874`, `rule.23.HostExport-Context-Move-Err@L86890`, `requirement.23.HostedExportSessionHandleValidity@L86908`, `requirement.23.HostedExportCapabilityIsolation@L86921`, `requirement.23.HostedSessionRootCapsGrant@L86934`, `requirement.23.HostedExportBoundaryEntrySequence@L86947`, `requirement.23.HostedExportInvalidHandleBehavior@L86966`, `requirement.23.HostedExportCatchFailureReturn@L86982`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L86997`, `requirement.23.HostedExportThunkAbiDetermination@L87010`, `def.23.HostThunkCarrierHelpers@L87028`, `rule.23.HostThunkParamCarrier-ByRef@L87056`, `rule.23.HostThunkParamCarrier-ByValue-Default@L87072`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87088`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87104`, `rule.23.HostThunkRetCarrier-Default@L87120`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87136`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87152`, `requirement.23.HostedExportThunkShapeUse@L87168`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87181`, `requirement.23.HostedExportNoExtraAbiRewriting@L87194`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87207`, `requirement.23.HostedThunkToSourceCallReconstruction@L87220`, `requirement.23.HostedStateSymbolResolution@L87233`
- `requirement.23.HostedLibraryLifecycleExports@L87246`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87263`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87276`, `requirement.23.HostedSessionHandleNoReissue@L87289`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87302`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87321`, `diagnostics.23.HostedExportDiagnostics@L87336`, `diagnostics.23.HostedExportDiagnosticOwnership@L87355`
- `grammar.23.FfiAttributes@L87372`, `requirement.23.FfiAttributesParsing@L87401`, `ast.23.FfiAttributesAttachedEntries@L87416`, `ast.23.FfiAttributeTargets@L87429`, `requirement.23.MangleAttributeSemantics@L87453`, `def.23.LibraryLinkKinds@L87472`, `requirement.23.LibraryAttributeSemantics@L87492`, `def.23.ResolveLibraryName@L87509`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87536`, `requirement.23.RawDylibResolution@L87549`, `def.23.UnwindModes@L87567`, `requirement.23.UnwindDefaultMode@L87585`, `requirement.23.UnwindAttributeTargetValidity@L87598`, `requirement.23.UnwindCatchAbiRequirement@L87611`, `requirement.23.ExportAttributeSemantics@L87631`, `requirement.23.HostExportAttributeSemantics@L87651`
- `requirement.23.FfiPassByValueAttributeSemantics@L87673`, `requirement.23.FfiAttributeConstraints@L87686`, `requirement.23.FfiAttributesDynamicSemantics@L87712`, `requirement.23.FfiAttributesLowering@L87727`, `diagnostics.23.FfiAttributeDiagnostics@L87742`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87775`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87790`, `ast.23.CapabilityIsolationNoDedicatedAst@L87805`
- `requirement.23.CapabilityIsolationSemantics@L87820`, `def.23.CapabilityIsolationHelpers@L87836`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87853`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87871`, `requirement.23.CapabilityIsolationDynamicSemantics@L87891`, `requirement.23.CapabilityIsolationLowering@L87906`, `diagnostics.23.CapabilityIsolationDiagnostics@L87921`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87936`
- `grammar.23.ForeignContracts@L87953`, `def.23.ForeignContractStart@L87978`, `rule.23.Parse-ForeignContractClauseListOpt-None@L87991`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L88007`, `rule.23.Parse-ForeignContractClauseList-Cons@L88023`, `rule.23.Parse-ForeignContractClauseListTail-End@L88039`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L88055`, `rule.23.Parse-ForeignContractClause-Assumes@L88071`
- `rule.23.Parse-ForeignContractClause-Ensures@L88087`, `def.23.ForeignEnsuresKindAndExpr@L88103`, `rule.23.Parse-EnsuresPredicate-Error@L88122`, `rule.23.Parse-EnsuresPredicate-NullResult@L88138`, `rule.23.Parse-EnsuresPredicate-Plain@L88154`, `ast.23.ForeignContractsForm@L88171`, `ast.23.EnsuresPredicateForms@L88194`, `def.23.ForeignPreconditions@L88216`
- `requirement.23.ForeignPredicateContext@L88229`, `def.23.ForeignPreconditionVerificationModes@L88255`, `requirement.23.ForeignPreconditionVerificationLowering@L88273`, `def.23.ForeignPostconditions@L88288`, `requirement.23.ForeignPostconditionPredicateBindings@L88301`, `def.23.ForeignPostconditionClassification@L88321`, `requirement.23.NullResultWellFormedness@L88354`, `def.23.NullableFfiResult@L88370`
- `rule.23.ForeignEnsures-NullResult-Err@L88386`, `requirement.23.ErrorPredicateWellFormedness@L88405`, `def.23.ForeignPostconditionVerificationModes@L88418`, `requirement.23.ForeignPostconditionStaticVerification@L88436`, `def.23.ForeignContractVerificationSummary@L88451`, `requirement.23.ForeignPreconditionDynamicFailure@L88471`, `requirement.23.ForeignPostconditionDynamicChecks@L88484`, `requirement.23.ForeignContractsLowering@L88499`
- `diagnostics.23.ForeignContractDiagnostics@L88514`, `requirement.23.BoundaryUnwindingSyntax@L88541`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88556`, `ast.23.BoundaryUnwindPolicySource@L88571`, `def.23.UnwindModeAstHelpers@L88584`, `def.23.DetermineUnwindMode@L88606`, `def.23.ParseUnwindArg@L88631`, `rule.23.UnwindMode-Valid@L88653`
- `rule.23.UnwindMode-Invalid-Err@L88671`, `requirement.23.BoundaryUnwindDynamicEffects@L88691`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88710`, `def.23.BoundaryUnwindCodeGenerationEffects@L88725`, `rule.23.CodeGen-UnwindAbort-Import@L88745`, `rule.23.CodeGen-UnwindCatch-Import@L88763`, `rule.23.CodeGen-UnwindAbort-Export@L88781`, `rule.23.CodeGen-UnwindCatch-Export@L88799`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88819`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88832`, `diagnostics.23.FfiDiagnosticsSupplement@L88847`

#### `spec.lowering`

Count: 158 total; 158 required; 0 recommended; 0 informative. Ledger line span: L88499-L91244.

- `requirement.24.SharedLoweringScope@L88868`, `def.24.CodegenModelAndTargets@L88883`, `def.24.CodegenJudgements@L88902`, `def.24.IRDefined@L88915`, `def.24.CodegenCorrectnessPredicates@L88928`, `def.24.CodegenCorrectAndUndefined@L88945`, `def.24.IRFormsAndEmissionJudgements@L88962`, `def.24.PanicOutCodegenParams@L88980`
- `def.24.MethodAndTransitionParams@L88994`, `def.24.SeqIR@L89012`, `def.24.EvalOrderJudgements@L89027`, `def.24.ChildExpressionListHelpers@L89040`, `def.24.ChildrenLTRExpressions@L89086`, `def.24.LowerExprJudgementsAndRetType@L89137`, `rule.24.Lower-Expr-Correctness@L89152`, `def.24.LowerExprTotal@L89168`
- `def.24.ExecIRJudgements@L89182`, `rule.24.ExecIR-ReadVar@L89195`, `rule.24.ExecIR-ReadPath@L89211`, `rule.24.ExecIR-StoreVar@L89227`, `rule.24.ExecIR-StoreVarNoDrop@L89243`, `rule.24.ExecIR-BindVar@L89259`, `rule.24.ExecIR-ReadPtr@L89275`, `rule.24.ExecIR-WritePtr@L89291`
- `def.24.AllocTarget@L89307`, `rule.24.ExecIR-Alloc@L89321`, `rule.24.MoveState-Root@L89337`, `rule.24.MoveState-Field@L89353`, `rule.24.ExecIR-MoveState@L89369`, `def.24.ExecIRControlResults@L89385`, `rule.24.ExecIR-Defer@L89401`, `def.24.ExecIRBlockHelpers@L89417`
- `rule.24.ExecIR-If-True@L89432`, `rule.24.ExecIR-If-False@L89448`, `rule.24.ExecIR-Block@L89464`, `rule.24.ExecIR-IfCase@L89480`, `rule.24.ExecIR-Loop-Infinite-Step@L89496`, `rule.24.ExecIR-Loop-Infinite-Continue@L89512`, `rule.24.ExecIR-Loop-Infinite-Break@L89528`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89544`
- `rule.24.ExecIR-Loop-Cond-False@L89560`, `rule.24.ExecIR-Loop-Cond-True-Step@L89576`, `rule.24.ExecIR-Loop-Cond-Continue@L89592`, `rule.24.ExecIR-Loop-Cond-Break@L89608`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89624`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89640`, `def.24.LoopIterIRJudgement@L89656`, `rule.24.ExecIR-Loop-Iter@L89669`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89685`, `rule.24.LoopIterIR-Done@L89701`, `rule.24.LoopIterIR-Step-Val@L89717`, `rule.24.LoopIterIR-Step-Continue@L89733`, `rule.24.LoopIterIR-Step-Break@L89749`, `rule.24.LoopIterIR-Step-Ctrl@L89765`, `rule.24.ExecIR-Region@L89781`, `rule.24.ExecIR-Frame-Implicit@L89797`
- `rule.24.ExecIR-Frame-Explicit@L89813`, `rule.24.LowerList-Empty@L89829`, `rule.24.LowerList-Cons@L89844`, `rule.24.LowerFieldInits-Empty@L89860`, `rule.24.LowerFieldInits-Cons@L89875`, `rule.24.LowerOpt-None@L89891`, `rule.24.LowerOpt-Some@L89906`, `def.24.RefSyms@L89922`
- `def.24.ExpandIR@L89984`, `def.24.UniqueEmits@L89997`, `def.24.ModuleItems@L90019`, `rule.24.CG-Project@L90032`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L90048`, `rule.24.CG-Module@L90061`, `rule.24.CG-Expr@L90077`, `rule.24.CG-Stmt@L90093`
- `rule.24.CG-Block@L90109`, `rule.24.CG-Place@L90125`, `rule.24.LowerIR-Module@L90141`, `rule.24.LowerIR-Err@L90157`, `rule.24.EmitLLVM-Ok@L90173`, `def.24.LLVMText21Acceptance@L90189`, `rule.24.EmitLLVM-Err@L90203`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90219`
- `rule.24.EmitObj-Ok@L90233`, `def.24.LLVMEmitObj21@L90249`, `rule.24.EmitObj-Err@L90262`, `def.24.PointerPrimitiveSizeAndAlignment@L90282`, `def.24.LayoutJudgements@L90337`, `rule.24.Size-Prim@L90350`, `rule.24.Align-Prim@L90366`, `rule.24.Layout-Prim@L90382`
- `def.24.ConstantEncodingHelpers@L90398`, `rule.24.Encode-Bool@L90415`, `rule.24.Encode-Char@L90431`, `rule.24.Encode-Int@L90447`, `rule.24.Encode-Float@L90463`, `rule.24.Encode-Unit@L90479`, `rule.24.Encode-Never@L90495`, `rule.24.Encode-RawPtr-Null@L90511`
- `def.24.ValidValueJudgement@L90527`, `rule.24.Valid-Bool@L90540`, `rule.24.Valid-Char@L90554`, `rule.24.Valid-Scalar@L90568`, `rule.24.Valid-Unit@L90583`, `rule.24.Valid-Never@L90597`, `def.24.ValidValueFallback@L90611`, `rule.24.Layout-Perm@L90627`
- `rule.24.Size-Perm@L90643`, `rule.24.Align-Perm@L90659`, `def.24.ValueBitsPerm@L90675`, `rule.24.Size-Ptr@L90688`, `rule.24.Align-Ptr@L90704`, `rule.24.Layout-Ptr@L90720`, `rule.24.Size-RawPtr@L90736`, `rule.24.Align-RawPtr@L90752`
- `rule.24.Layout-RawPtr@L90768`, `rule.24.Size-Func@L90784`, `rule.24.Align-Func@L90800`, `rule.24.Layout-Func@L90816`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90834`, `def.24.ExternAbiSetAndConventionMapping@L90914`, `def.24.ConventionLayout@L90936`, `def.24.AssignParamRegs@L90986`
- `def.24.StackFrameForm@L91012`, `rule.24.StackFrame-Layout@L91032`, `rule.24.Conv-Compatible@L91049`, `rule.24.Conv-FFI-Required@L91065`, `def.24.ABITypeAndABITyJudgement@L91083`, `rule.24.ABI-Prim@L91097`, `rule.24.ABI-Perm@L91113`, `rule.24.ABI-Ptr@L91129`
- `rule.24.ABI-RawPtr@L91145`, `rule.24.ABI-Func@L91161`, `rule.24.ABI-Alias@L91177`, `rule.24.ABI-Record@L91193`, `rule.24.ABI-Tuple@L91209`, `rule.24.ABI-Array@L91225`, `rule.24.ABI-Slice@L91241`, `rule.24.ABI-Range@L91257`
- `rule.24.ABI-RangeInclusive@L91273`, `rule.24.ABI-RangeFrom@L91289`, `rule.24.ABI-RangeTo@L91305`, `rule.24.ABI-RangeToInclusive@L91321`, `rule.24.ABI-RangeFull@L91337`, `rule.24.ABI-Enum@L91353`, `rule.24.ABI-Union@L91369`, `rule.24.ABI-Modal@L91385`
- `rule.24.ABI-Dynamic@L91401`, `rule.24.ABI-StringBytes@L91417`, `def.24.ABIParameterReturnPassingHelpers@L91435`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91456`, `rule.24.ABI-Param-ByRef-Alias@L91469`, `rule.24.ABI-Param-ByValue-Move@L91485`, `rule.24.ABI-Param-ByRef-Move@L91501`, `rule.24.ABI-Ret-ByValue@L91517`
- `rule.24.ABI-Ret-ByRef@L91533`, `rule.24.ABI-Call@L91549`, `rule.24.ABI-ForeignParam-ByValue@L91565`, `rule.24.ABI-ForeignParam-ByRef@L91581`, `rule.24.ABI-ForeignCall@L91597`, `def.24.PanicRecordAndPanicOut@L91613`
- `requirement.24.SharedLoweringScope@L88868`, `def.24.CodegenModelAndTargets@L88883`, `def.24.CodegenJudgements@L88902`, `def.24.IRDefined@L88915`, `def.24.CodegenCorrectnessPredicates@L88928`, `def.24.CodegenCorrectAndUndefined@L88945`, `def.24.IRFormsAndEmissionJudgements@L88962`, `def.24.PanicOutCodegenParams@L88980`
- `def.24.MethodAndTransitionParams@L88994`, `def.24.SeqIR@L89012`, `def.24.EvalOrderJudgements@L89027`, `def.24.ChildExpressionListHelpers@L89040`, `def.24.ChildrenLTRExpressions@L89086`, `def.24.LowerExprJudgementsAndRetType@L89137`, `rule.24.Lower-Expr-Correctness@L89152`, `def.24.LowerExprTotal@L89168`
- `def.24.ExecIRJudgements@L89182`, `rule.24.ExecIR-ReadVar@L89195`, `rule.24.ExecIR-ReadPath@L89211`, `rule.24.ExecIR-StoreVar@L89227`, `rule.24.ExecIR-StoreVarNoDrop@L89243`, `rule.24.ExecIR-BindVar@L89259`, `rule.24.ExecIR-ReadPtr@L89275`, `rule.24.ExecIR-WritePtr@L89291`
- `def.24.AllocTarget@L89307`, `rule.24.ExecIR-Alloc@L89321`, `rule.24.MoveState-Root@L89337`, `rule.24.MoveState-Field@L89353`, `rule.24.ExecIR-MoveState@L89369`, `def.24.ExecIRControlResults@L89385`, `rule.24.ExecIR-Defer@L89401`, `def.24.ExecIRBlockHelpers@L89417`
- `rule.24.ExecIR-If-True@L89432`, `rule.24.ExecIR-If-False@L89448`, `rule.24.ExecIR-Block@L89464`, `rule.24.ExecIR-IfCase@L89480`, `rule.24.ExecIR-Loop-Infinite-Step@L89496`, `rule.24.ExecIR-Loop-Infinite-Continue@L89512`, `rule.24.ExecIR-Loop-Infinite-Break@L89528`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89544`
- `rule.24.ExecIR-Loop-Cond-False@L89560`, `rule.24.ExecIR-Loop-Cond-True-Step@L89576`, `rule.24.ExecIR-Loop-Cond-Continue@L89592`, `rule.24.ExecIR-Loop-Cond-Break@L89608`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89624`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89640`, `def.24.LoopIterIRJudgement@L89656`, `rule.24.ExecIR-Loop-Iter@L89669`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89685`, `rule.24.LoopIterIR-Done@L89701`, `rule.24.LoopIterIR-Step-Val@L89717`, `rule.24.LoopIterIR-Step-Continue@L89733`, `rule.24.LoopIterIR-Step-Break@L89749`, `rule.24.LoopIterIR-Step-Ctrl@L89765`, `rule.24.ExecIR-Region@L89781`, `rule.24.ExecIR-Frame-Implicit@L89797`
- `rule.24.ExecIR-Frame-Explicit@L89813`, `rule.24.LowerList-Empty@L89829`, `rule.24.LowerList-Cons@L89844`, `rule.24.LowerFieldInits-Empty@L89860`, `rule.24.LowerFieldInits-Cons@L89875`, `rule.24.LowerOpt-None@L89891`, `rule.24.LowerOpt-Some@L89906`, `def.24.RefSyms@L89922`
- `def.24.ExpandIR@L89984`, `def.24.UniqueEmits@L89997`, `def.24.ModuleItems@L90019`, `rule.24.CG-Project@L90032`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L90048`, `rule.24.CG-Module@L90061`, `rule.24.CG-Expr@L90077`, `rule.24.CG-Stmt@L90093`
- `rule.24.CG-Block@L90109`, `rule.24.CG-Place@L90125`, `rule.24.LowerIR-Module@L90141`, `rule.24.LowerIR-Err@L90157`, `rule.24.EmitLLVM-Ok@L90173`, `def.24.LLVMText21Acceptance@L90189`, `rule.24.EmitLLVM-Err@L90203`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90219`
- `rule.24.EmitObj-Ok@L90233`, `def.24.LLVMEmitObj21@L90249`, `rule.24.EmitObj-Err@L90262`, `def.24.PointerPrimitiveSizeAndAlignment@L90282`, `def.24.LayoutJudgements@L90337`, `rule.24.Size-Prim@L90350`, `rule.24.Align-Prim@L90366`, `rule.24.Layout-Prim@L90382`
- `def.24.ConstantEncodingHelpers@L90398`, `rule.24.Encode-Bool@L90415`, `rule.24.Encode-Char@L90431`, `rule.24.Encode-Int@L90447`, `rule.24.Encode-Float@L90463`, `rule.24.Encode-Unit@L90479`, `rule.24.Encode-Never@L90495`, `rule.24.Encode-RawPtr-Null@L90511`
- `def.24.ValidValueJudgement@L90527`, `rule.24.Valid-Bool@L90540`, `rule.24.Valid-Char@L90554`, `rule.24.Valid-Scalar@L90568`, `rule.24.Valid-Unit@L90583`, `rule.24.Valid-Never@L90597`, `def.24.ValidValueFallback@L90611`, `rule.24.Layout-Perm@L90627`
- `rule.24.Size-Perm@L90643`, `rule.24.Align-Perm@L90659`, `def.24.ValueBitsPerm@L90675`, `rule.24.Size-Ptr@L90688`, `rule.24.Align-Ptr@L90704`, `rule.24.Layout-Ptr@L90720`, `rule.24.Size-RawPtr@L90736`, `rule.24.Align-RawPtr@L90752`
- `rule.24.Layout-RawPtr@L90768`, `rule.24.Size-Func@L90784`, `rule.24.Align-Func@L90800`, `rule.24.Layout-Func@L90816`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90834`, `def.24.ExternAbiSetAndConventionMapping@L90914`, `def.24.ConventionLayout@L90936`, `def.24.AssignParamRegs@L90986`
- `def.24.StackFrameForm@L91012`, `rule.24.StackFrame-Layout@L91032`, `rule.24.Conv-Compatible@L91049`, `rule.24.Conv-FFI-Required@L91065`, `def.24.ABITypeAndABITyJudgement@L91083`, `rule.24.ABI-Prim@L91097`, `rule.24.ABI-Perm@L91113`, `rule.24.ABI-Ptr@L91129`
- `rule.24.ABI-RawPtr@L91145`, `rule.24.ABI-Func@L91161`, `rule.24.ABI-Alias@L91177`, `rule.24.ABI-Record@L91193`, `rule.24.ABI-Tuple@L91209`, `rule.24.ABI-Array@L91225`, `rule.24.ABI-Slice@L91241`, `rule.24.ABI-Range@L91257`
- `rule.24.ABI-RangeInclusive@L91273`, `rule.24.ABI-RangeFrom@L91289`, `rule.24.ABI-RangeTo@L91305`, `rule.24.ABI-RangeToInclusive@L91321`, `rule.24.ABI-RangeFull@L91337`, `rule.24.ABI-Enum@L91353`, `rule.24.ABI-Union@L91369`, `rule.24.ABI-Modal@L91385`
- `rule.24.ABI-Dynamic@L91401`, `rule.24.ABI-StringBytes@L91417`, `def.24.ABIParameterReturnPassingHelpers@L91435`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91456`, `rule.24.ABI-Param-ByRef-Alias@L91469`, `rule.24.ABI-Param-ByValue-Move@L91485`, `rule.24.ABI-Param-ByRef-Move@L91501`, `rule.24.ABI-Ret-ByValue@L91517`
- `rule.24.ABI-Ret-ByRef@L91533`, `rule.24.ABI-Call@L91549`, `rule.24.ABI-ForeignParam-ByValue@L91565`, `rule.24.ABI-ForeignParam-ByRef@L91581`, `rule.24.ABI-ForeignCall@L91597`, `def.24.PanicRecordAndPanicOut@L91613`

#### `spec.symbols`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L91273-L92088.

- `def.24.MangleJudgementAndConstructors@L91642`, `def.24.PathSymbolHelpers@L91658`, `def.24.ItemPath@L91675`, `def.24.PathOfTypeAndClassPath@L91697`, `def.24.LiteralSymbolHashing@L91718`, `def.24.ScopedRawAndHostBodySymbols@L91737`, `def.24.AttributeSymbolHelpers@L91752`, `def.24.ExternAbiSymbolHelpers@L91772`
- `def.24.LinkName@L91791`, `def.24.HostThunkLinkNameAndItemName@L91809`, `rule.24.Mangle-HostExport-Proc@L91826`, `rule.24.Mangle-Proc@L91842`, `rule.24.Mangle-ExternProc@L91858`, `rule.24.Mangle-Main@L91874`, `rule.24.Mangle-Record-Method@L91890`, `rule.24.Mangle-Class-Method@L91906`
- `rule.24.Mangle-State-Method@L91922`, `rule.24.Mangle-Transition@L91938`, `rule.24.Mangle-Static@L91954`, `rule.24.Mangle-StaticBinding@L91970`, `rule.24.Mangle-VTable@L91986`, `rule.24.Mangle-Literal@L92002`, `rule.24.Mangle-DefaultImpl@L92018`, `req.24.ClosureIndexUniqueness@L92034`
- `def.24.EnclosingSym@L92047`, `rule.24.Mangle-Closure@L92060`, `rule.24.Mangle-ClosureEnv@L92076`, `def.24.ClosureCodeSym@L92092`, `def.24.LinkageDefinitions@L92107`, `rule.24.Linkage-UserItem@L92121`, `rule.24.Linkage-ExternProc@L92137`, `rule.24.Linkage-UserItem-Internal@L92153`
- `rule.24.Linkage-StaticBinding@L92169`, `rule.24.Linkage-StaticBinding-Internal@L92185`, `rule.24.Linkage-ClassMethod@L92201`, `rule.24.Linkage-ClassMethod-Internal@L92217`, `rule.24.Linkage-StateMethod@L92233`, `rule.24.Linkage-StateMethod-Internal@L92249`, `rule.24.Linkage-Transition@L92265`, `rule.24.Linkage-Transition-Internal@L92281`
- `rule.24.Linkage-InitFn@L92297`, `rule.24.Linkage-DeinitFn@L92313`, `rule.24.Linkage-VTable@L92329`, `rule.24.Linkage-LiteralData@L92345`, `rule.24.Linkage-DropGlue@L92361`, `rule.24.Linkage-DefaultImpl@L92377`, `rule.24.Linkage-DefaultImpl-Internal@L92393`, `rule.24.Linkage-PanicSym@L92409`
- `rule.24.Linkage-BuiltinModalSym@L92425`, `rule.24.Linkage-BuiltinSym@L92441`, `rule.24.Linkage-EntrySym@L92457`
- `def.24.MangleJudgementAndConstructors@L91642`, `def.24.PathSymbolHelpers@L91658`, `def.24.ItemPath@L91675`, `def.24.PathOfTypeAndClassPath@L91697`, `def.24.LiteralSymbolHashing@L91718`, `def.24.ScopedRawAndHostBodySymbols@L91737`, `def.24.AttributeSymbolHelpers@L91752`, `def.24.ExternAbiSymbolHelpers@L91772`
- `def.24.LinkName@L91791`, `def.24.HostThunkLinkNameAndItemName@L91809`, `rule.24.Mangle-HostExport-Proc@L91826`, `rule.24.Mangle-Proc@L91842`, `rule.24.Mangle-ExternProc@L91858`, `rule.24.Mangle-Main@L91874`, `rule.24.Mangle-Record-Method@L91890`, `rule.24.Mangle-Class-Method@L91906`
- `rule.24.Mangle-State-Method@L91922`, `rule.24.Mangle-Transition@L91938`, `rule.24.Mangle-Static@L91954`, `rule.24.Mangle-StaticBinding@L91970`, `rule.24.Mangle-VTable@L91986`, `rule.24.Mangle-Literal@L92002`, `rule.24.Mangle-DefaultImpl@L92018`, `req.24.ClosureIndexUniqueness@L92034`
- `def.24.EnclosingSym@L92047`, `rule.24.Mangle-Closure@L92060`, `rule.24.Mangle-ClosureEnv@L92076`, `def.24.ClosureCodeSym@L92092`, `def.24.LinkageDefinitions@L92107`, `rule.24.Linkage-UserItem@L92121`, `rule.24.Linkage-ExternProc@L92137`, `rule.24.Linkage-UserItem-Internal@L92153`
- `rule.24.Linkage-StaticBinding@L92169`, `rule.24.Linkage-StaticBinding-Internal@L92185`, `rule.24.Linkage-ClassMethod@L92201`, `rule.24.Linkage-ClassMethod-Internal@L92217`, `rule.24.Linkage-StateMethod@L92233`, `rule.24.Linkage-StateMethod-Internal@L92249`, `rule.24.Linkage-Transition@L92265`, `rule.24.Linkage-Transition-Internal@L92281`
- `rule.24.Linkage-InitFn@L92297`, `rule.24.Linkage-DeinitFn@L92313`, `rule.24.Linkage-VTable@L92329`, `rule.24.Linkage-LiteralData@L92345`, `rule.24.Linkage-DropGlue@L92361`, `rule.24.Linkage-DefaultImpl@L92377`, `rule.24.Linkage-DefaultImpl-Internal@L92393`, `rule.24.Linkage-PanicSym@L92409`
- `rule.24.Linkage-BuiltinModalSym@L92425`, `rule.24.Linkage-BuiltinSym@L92441`, `rule.24.Linkage-EntrySym@L92457`

#### `spec.initialization`

Count: 102 total; 102 required; 0 recommended; 0 informative. Ledger line span: L92108-L93623.

- `def.24.GlobalsJudg@L92477`, `def.24.ConstInitJudg@L92490`, `def.24.ConstInitLiteral@L92503`, `def.24.StaticName@L92516`, `def.24.StaticBindTypes@L92531`, `def.24.StaticBindList@L92544`, `def.24.StaticBinding@L92557`, `def.24.StaticSym@L92570`
- `rule.24.Emit-Static-Const@L92585`, `rule.24.Emit-Static-Init@L92601`, `rule.24.Emit-Static-Multi@L92617`, `def.24.InitSym@L92633`, `rule.24.InitFn@L92646`, `def.24.DeinitSym@L92662`, `rule.24.DeinitFn@L92675`, `def.24.StaticItems@L92691`
- `def.24.StaticItemOf@L92704`, `def.24.StaticSymPath@L92717`, `def.24.StaticAddr@L92730`, `req.24.HostedStaticAddrSessionInterpretation@L92743`, `def.24.AddrOfSym@L92756`, `def.24.StaticType@L92769`, `def.24.StaticBindInfo@L92782`, `def.24.SeqIRList@L92795`
- `def.24.StaticStoreIR@L92809`, `rule.24.Lower-StaticInit-Item@L92823`, `rule.24.Lower-StaticInitItems-Empty@L92839`, `rule.24.Lower-StaticInitItems-Cons@L92854`, `rule.24.Lower-StaticInit@L92870`, `rule.24.InitCallIR@L92886`, `def.24.Rev@L92902`, `rule.24.Lower-StaticDeinitNames-Empty@L92916`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92931`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92947`, `rule.24.Lower-StaticDeinit-Item@L92963`, `rule.24.Lower-StaticDeinitItems-Empty@L92979`, `rule.24.Lower-StaticDeinitItems-Cons@L92994`, `rule.24.Lower-StaticDeinit@L93010`, `rule.24.DeinitCallIR@L93026`, `def.24.HostedStateAddressDefinitions@L93042`
- `def.24.LibraryStateSymbolDefinitions@L93057`, `def.24.HostedStateJudg@L93073`, `req.24.SessionStateInitDefinesHostedCells@L93086`, `req.24.SessionStateDestroyRemovesHostedCells@L93099`, `req.24.HostedLibraryStateAddressInterpretation@L93112`, `def.24.InitializationGraphOrdering@L93129`, `rule.24.Topo-Ok@L93148`, `rule.24.Topo-Cycle@L93164`
- `def.24.ProjectInitializationItems@L93180`, `def.24.InitializationPlanDefinitions@L93196`, `def.24.EvalFromEvalSigma@L93218`, `rule.24.EmitInitPlan@L93232`, `rule.24.EmitInitPlan-Err@L93248`, `rule.24.EmitDeinitPlan@L93264`, `rule.24.EmitDeinitPlan-Err@L93280`, `def.24.InitStateMachineDefinitions@L93296`
- `rule.24.Init-Start@L93311`, `rule.24.Init-Step@L93326`, `rule.24.Init-Next-Module@L93342`, `rule.24.Init-Panic@L93358`, `rule.24.Init-Done@L93374`, `rule.24.Init-Ok@L93390`, `rule.24.Init-Fail@L93406`, `rule.24.Deinit-Ok@L93422`
- `rule.24.Deinit-Panic@L93438`, `def.24.EntryJudg@L93456`, `rule.24.EntrySym-Decl@L93469`, `rule.24.ContextInitSym-Decl@L93484`, `def.24.PanicRecordInit@L93499`, `def.24.EntryStubSpec@L93512`, `rule.24.EntryStub-Decl@L93530`, `rule.24.EntrySym-Err@L93546`
- `rule.24.EntryStub-Err@L93562`, `def.24.LibraryImageJudg@L93580`, `def.24.LibraryImageStateDefinitions@L93593`, `req.24.DistinctLibraryImageState@L93613`, `req.24.LibraryImageLivenessTransitions@L93626`, `req.24.LibraryImageInitDefinesSharedCells@L93639`, `req.24.LibraryImageDestroyRemovesSharedCells@L93652`, `req.24.SharedLibraryImageStateInterpretation@L93665`
- `req.24.PartialInitPanicCleanupPrefix@L93678`, `req.24.RawExportImageLifecycle@L93691`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93704`, `req.24.SharedLibraryLoaderEntrypoint@L93717`, `rule.24.LibraryImageInitSigma@L93730`, `rule.24.RawLibraryCallSigma-Ok@L93746`, `rule.24.LibraryImageDestroySigma@L93762`, `def.24.HostedSessionJudg@L93778`
- `def.24.HostedSessionStateDefinitions@L93791`, `req.24.DistinctHostedState@L93813`, `req.24.HostedSessionLifecycleState@L93826`, `req.24.HostedSessionNoConcurrentReentry@L93839`, `rule.24.HostSessionInitSigma@L93852`, `rule.24.HostedCallSigma-Ok@L93868`, `rule.24.HostSessionDestroySigma@L93884`, `def.24.InterpJudg@L93902`
- `def.24.ContextValue@L93915`, `rule.24.ContextInitSigma@L93928`, `rule.24.Interpret-Project-Ok@L93944`, `rule.24.Interpret-Project-Init-Panic@L93960`, `rule.24.Interpret-Project-Main-Ctrl@L93976`, `rule.24.Interpret-Project-Deinit-Panic@L93992`
- `def.24.GlobalsJudg@L92477`, `def.24.ConstInitJudg@L92490`, `def.24.ConstInitLiteral@L92503`, `def.24.StaticName@L92516`, `def.24.StaticBindTypes@L92531`, `def.24.StaticBindList@L92544`, `def.24.StaticBinding@L92557`, `def.24.StaticSym@L92570`
- `rule.24.Emit-Static-Const@L92585`, `rule.24.Emit-Static-Init@L92601`, `rule.24.Emit-Static-Multi@L92617`, `def.24.InitSym@L92633`, `rule.24.InitFn@L92646`, `def.24.DeinitSym@L92662`, `rule.24.DeinitFn@L92675`, `def.24.StaticItems@L92691`
- `def.24.StaticItemOf@L92704`, `def.24.StaticSymPath@L92717`, `def.24.StaticAddr@L92730`, `req.24.HostedStaticAddrSessionInterpretation@L92743`, `def.24.AddrOfSym@L92756`, `def.24.StaticType@L92769`, `def.24.StaticBindInfo@L92782`, `def.24.SeqIRList@L92795`
- `def.24.StaticStoreIR@L92809`, `rule.24.Lower-StaticInit-Item@L92823`, `rule.24.Lower-StaticInitItems-Empty@L92839`, `rule.24.Lower-StaticInitItems-Cons@L92854`, `rule.24.Lower-StaticInit@L92870`, `rule.24.InitCallIR@L92886`, `def.24.Rev@L92902`, `rule.24.Lower-StaticDeinitNames-Empty@L92916`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92931`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92947`, `rule.24.Lower-StaticDeinit-Item@L92963`, `rule.24.Lower-StaticDeinitItems-Empty@L92979`, `rule.24.Lower-StaticDeinitItems-Cons@L92994`, `rule.24.Lower-StaticDeinit@L93010`, `rule.24.DeinitCallIR@L93026`, `def.24.HostedStateAddressDefinitions@L93042`
- `def.24.LibraryStateSymbolDefinitions@L93057`, `def.24.HostedStateJudg@L93073`, `req.24.SessionStateInitDefinesHostedCells@L93086`, `req.24.SessionStateDestroyRemovesHostedCells@L93099`, `req.24.HostedLibraryStateAddressInterpretation@L93112`, `def.24.InitializationGraphOrdering@L93129`, `rule.24.Topo-Ok@L93148`, `rule.24.Topo-Cycle@L93164`
- `def.24.ProjectInitializationItems@L93180`, `def.24.InitializationPlanDefinitions@L93196`, `def.24.EvalFromEvalSigma@L93218`, `rule.24.EmitInitPlan@L93232`, `rule.24.EmitInitPlan-Err@L93248`, `rule.24.EmitDeinitPlan@L93264`, `rule.24.EmitDeinitPlan-Err@L93280`, `def.24.InitStateMachineDefinitions@L93296`
- `rule.24.Init-Start@L93311`, `rule.24.Init-Step@L93326`, `rule.24.Init-Next-Module@L93342`, `rule.24.Init-Panic@L93358`, `rule.24.Init-Done@L93374`, `rule.24.Init-Ok@L93390`, `rule.24.Init-Fail@L93406`, `rule.24.Deinit-Ok@L93422`
- `rule.24.Deinit-Panic@L93438`, `def.24.EntryJudg@L93456`, `rule.24.EntrySym-Decl@L93469`, `rule.24.ContextInitSym-Decl@L93484`, `def.24.PanicRecordInit@L93499`, `def.24.EntryStubSpec@L93512`, `rule.24.EntryStub-Decl@L93530`, `rule.24.EntrySym-Err@L93546`
- `rule.24.EntryStub-Err@L93562`, `def.24.LibraryImageJudg@L93580`, `def.24.LibraryImageStateDefinitions@L93593`, `req.24.DistinctLibraryImageState@L93613`, `req.24.LibraryImageLivenessTransitions@L93626`, `req.24.LibraryImageInitDefinesSharedCells@L93639`, `req.24.LibraryImageDestroyRemovesSharedCells@L93652`, `req.24.SharedLibraryImageStateInterpretation@L93665`
- `req.24.PartialInitPanicCleanupPrefix@L93678`, `req.24.RawExportImageLifecycle@L93691`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93704`, `req.24.SharedLibraryLoaderEntrypoint@L93717`, `rule.24.LibraryImageInitSigma@L93730`, `rule.24.RawLibraryCallSigma-Ok@L93746`, `rule.24.LibraryImageDestroySigma@L93762`, `def.24.HostedSessionJudg@L93778`
- `def.24.HostedSessionStateDefinitions@L93791`, `req.24.DistinctHostedState@L93813`, `req.24.HostedSessionLifecycleState@L93826`, `req.24.HostedSessionNoConcurrentReentry@L93839`, `rule.24.HostSessionInitSigma@L93852`, `rule.24.HostedCallSigma-Ok@L93868`, `rule.24.HostSessionDestroySigma@L93884`, `def.24.InterpJudg@L93902`
- `def.24.ContextValue@L93915`, `rule.24.ContextInitSigma@L93928`, `rule.24.Interpret-Project-Ok@L93944`, `rule.24.Interpret-Project-Init-Panic@L93960`, `rule.24.Interpret-Project-Main-Ctrl@L93976`, `rule.24.Interpret-Project-Deinit-Panic@L93992`

#### `spec.cleanup`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L93645-L94497.

- `def.24.CleanupJudg@L94014`, `rule.24.CleanupPlan@L94027`, `def.24.EmitDropSpec@L94043`, `def.24.PanicOutAddr@L94059`, `def.24.PanicRecordOf@L94072`, `def.24.WritePanicRecord@L94085`, `def.24.InitPanicHandle@L94098`, `req.24.InitPanicHandleResponsiblePrefix@L94111`
- `rule.24.PanicSym@L94124`, `def.24.PanicReasonCodes@L94139`, `def.24.PanicSites@L94164`, `def.24.ClearPanic@L94188`, `def.24.PanicCheck@L94201`, `def.24.LowerPanic@L94214`, `def.24.ResponsibleBinding@L94229`, `grammar.24.CleanupItem@L94242`
- `def.24.DropJudgmentDefinitions@L94255`, `def.24.RecordType@L94272`, `def.24.DropCall@L94285`, `def.24.ReleaseValue@L94302`, `def.24.DropChildren@L94316`, `def.24.DropList@L94336`, `rule.24.DropAction-Moved@L94350`, `rule.24.DropAction-Partial@L94366`
- `rule.24.DropAction-Valid@L94382`, `rule.24.DropStaticAction@L94398`, `def.24.NonRecordFOk@L94414`, `rule.24.DropValueOut-DropPanic@L94427`, `rule.24.DropValueOut-ChildPanic@L94443`, `rule.24.DropValueOut-Ok@L94459`, `def.24.CleanupStateDefinitions@L94477`, `rule.24.Cleanup-Start@L94491`
- `rule.24.Cleanup-Step-Drop-Ok@L94506`, `rule.24.Cleanup-Step-Drop-Panic@L94522`, `rule.24.Cleanup-Step-Drop-Abort@L94538`, `rule.24.Cleanup-Step-DropStatic-Ok@L94554`, `rule.24.Cleanup-Step-DropStatic-Panic@L94570`, `rule.24.Cleanup-Step-DropStatic-Abort@L94586`, `rule.24.Cleanup-Step-Defer-Ok@L94602`, `rule.24.Cleanup-Step-Defer-Panic@L94618`
- `rule.24.Cleanup-Step-Defer-Abort@L94634`, `rule.24.Cleanup-Done@L94650`, `rule.24.Destroy-Empty@L94666`, `rule.24.Destroy-Cons@L94681`, `def.24.CleanupJudgDyn@L94697`, `rule.24.Cleanup-Empty@L94710`, `rule.24.Cleanup-Cons-Drop@L94725`, `rule.24.Cleanup-Cons-Drop-Panic@L94741`
- `rule.24.Cleanup-Cons-DropStatic@L94757`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94773`, `rule.24.Cleanup-Cons-Defer-Ok@L94789`, `rule.24.Cleanup-Cons-Defer-Panic@L94805`, `def.24.CleanupScopeJudg@L94821`, `rule.24.CleanupScope-From-SmallStep@L94834`, `rule.24.Unwind-Step@L94850`, `rule.24.Unwind-Abort@L94866`
- `def.24.CleanupJudg@L94014`, `rule.24.CleanupPlan@L94027`, `def.24.EmitDropSpec@L94043`, `def.24.PanicOutAddr@L94059`, `def.24.PanicRecordOf@L94072`, `def.24.WritePanicRecord@L94085`, `def.24.InitPanicHandle@L94098`, `req.24.InitPanicHandleResponsiblePrefix@L94111`
- `rule.24.PanicSym@L94124`, `def.24.PanicReasonCodes@L94139`, `def.24.PanicSites@L94164`, `def.24.ClearPanic@L94188`, `def.24.PanicCheck@L94201`, `def.24.LowerPanic@L94214`, `def.24.ResponsibleBinding@L94229`, `grammar.24.CleanupItem@L94242`
- `def.24.DropJudgmentDefinitions@L94255`, `def.24.RecordType@L94272`, `def.24.DropCall@L94285`, `def.24.ReleaseValue@L94302`, `def.24.DropChildren@L94316`, `def.24.DropList@L94336`, `rule.24.DropAction-Moved@L94350`, `rule.24.DropAction-Partial@L94366`
- `rule.24.DropAction-Valid@L94382`, `rule.24.DropStaticAction@L94398`, `def.24.NonRecordFOk@L94414`, `rule.24.DropValueOut-DropPanic@L94427`, `rule.24.DropValueOut-ChildPanic@L94443`, `rule.24.DropValueOut-Ok@L94459`, `def.24.CleanupStateDefinitions@L94477`, `rule.24.Cleanup-Start@L94491`
- `rule.24.Cleanup-Step-Drop-Ok@L94506`, `rule.24.Cleanup-Step-Drop-Panic@L94522`, `rule.24.Cleanup-Step-Drop-Abort@L94538`, `rule.24.Cleanup-Step-DropStatic-Ok@L94554`, `rule.24.Cleanup-Step-DropStatic-Panic@L94570`, `rule.24.Cleanup-Step-DropStatic-Abort@L94586`, `rule.24.Cleanup-Step-Defer-Ok@L94602`, `rule.24.Cleanup-Step-Defer-Panic@L94618`
- `rule.24.Cleanup-Step-Defer-Abort@L94634`, `rule.24.Cleanup-Done@L94650`, `rule.24.Destroy-Empty@L94666`, `rule.24.Destroy-Cons@L94681`, `def.24.CleanupJudgDyn@L94697`, `rule.24.Cleanup-Empty@L94710`, `rule.24.Cleanup-Cons-Drop@L94725`, `rule.24.Cleanup-Cons-Drop-Panic@L94741`
- `rule.24.Cleanup-Cons-DropStatic@L94757`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94773`, `rule.24.Cleanup-Cons-Defer-Ok@L94789`, `rule.24.Cleanup-Cons-Defer-Panic@L94805`, `def.24.CleanupScopeJudg@L94821`, `rule.24.CleanupScope-From-SmallStep@L94834`, `rule.24.Unwind-Step@L94850`, `rule.24.Unwind-Abort@L94866`

#### `spec.runtime-interface`

Count: 64 total; 64 required; 0 recommended; 0 informative. Ledger line span: L94519-L95514.

- `def.24.RuntimeIfcJudg@L94888`, `def.24.BuiltinModalLayoutSpec@L94901`, `rule.24.BuiltinModalLayout@L94914`, `def.24.BuiltinModalSymMap@L94930`, `rule.24.BuiltinModalSym@L94957`, `rule.24.RegionAddr-AddrIsActive@L94973`, `rule.24.RegionAddr-AddrTagFrom@L94988`, `rule.24.BuiltinSym-FileSystem-OpenRead@L95003`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L95018`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L95033`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L95048`, `rule.24.BuiltinSym-FileSystem-ReadFile@L95063`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L95078`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95093`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95108`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95123`
- `rule.24.BuiltinSym-FileSystem-Exists@L95138`, `rule.24.BuiltinSym-FileSystem-Remove@L95153`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95168`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95183`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95198`, `rule.24.BuiltinSym-FileSystem-Kind@L95213`, `rule.24.BuiltinSym-FileSystem-Restrict@L95228`, `rule.24.BuiltinSym-Network-RestrictHost@L95243`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95258`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95273`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95288`, `rule.24.BuiltinSym-Reactor-Run@L95303`, `rule.24.BuiltinSym-Reactor-Register@L95318`, `rule.24.BuiltinSym-System-Exit@L95333`, `rule.24.BuiltinSym-System-GetEnv@L95348`, `rule.24.BuiltinSym-System-Run@L95408`
- `def.24.BuiltinSymJudg@L95425`, `def.24.StringBytesBuiltinMethodSets@L95438`, `def.24.StringBuiltinSymbols@L95454`, `def.24.BytesBuiltinSymbols@L95474`, `rule.24.BuiltinSym-String-Err@L95496`, `rule.24.BuiltinSym-Bytes-Err@L95512`, `def.24.DropHookJudg@L95528`, `rule.24.StringDropSym-Decl@L95541`
- `rule.24.BytesDropSym-Decl@L95556`, `rule.24.StringDropSym-Err@L95571`, `rule.24.BytesDropSym-Err@L95587`, `def.24.RuntimeDeclJudg@L95605`, `def.24.RuntimeMethodAndSymbolSets@L95618`, `def.24.CapabilityBuiltinSigs@L95637`, `def.24.CoreRuntimeSigs@L95655`, `def.24.BuiltinModalProcSigs@L95671`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95689`, `def.24.LLVMDeclType@L95708`, `rule.24.RuntimeDecls@L95721`, `def.24.RuntimeDeclarationCoverage@L95737`, `rule.24.Prim-Network-RestrictHost-Runtime@L95756`, `def.24.HeapJudg@L95772`, `req.24.HeapHostPrimitiveRelations@L95785`, `def.24.HeapStateAccountingDefinitions@L95798`
- `req.24.HeapPrimitiveSemantics@L95813`, `rule.24.Prim-Heap-WithQuota@L95839`, `rule.24.Prim-Heap-AllocRaw@L95855`, `rule.24.Prim-Heap-DeallocRaw@L95871`, `def.24.ReactorJudg@L95887`, `req.24.ReactorHostPrimitiveRelations@L95900`, `rule.24.Prim-Reactor-Run@L95913`, `rule.24.Prim-Reactor-Register@L95929`
- `def.24.RuntimeIfcJudg@L94888`, `def.24.BuiltinModalLayoutSpec@L94901`, `rule.24.BuiltinModalLayout@L94914`, `def.24.BuiltinModalSymMap@L94930`, `rule.24.BuiltinModalSym@L94957`, `rule.24.RegionAddr-AddrIsActive@L94973`, `rule.24.RegionAddr-AddrTagFrom@L94988`, `rule.24.BuiltinSym-FileSystem-OpenRead@L95003`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L95018`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L95033`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L95048`, `rule.24.BuiltinSym-FileSystem-ReadFile@L95063`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L95078`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95093`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95108`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95123`
- `rule.24.BuiltinSym-FileSystem-Exists@L95138`, `rule.24.BuiltinSym-FileSystem-Remove@L95153`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95168`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95183`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95198`, `rule.24.BuiltinSym-FileSystem-Kind@L95213`, `rule.24.BuiltinSym-FileSystem-Restrict@L95228`, `rule.24.BuiltinSym-Network-RestrictHost@L95243`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95258`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95273`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95288`, `rule.24.BuiltinSym-Reactor-Run@L95303`, `rule.24.BuiltinSym-Reactor-Register@L95318`, `rule.24.BuiltinSym-System-Exit@L95333`, `rule.24.BuiltinSym-System-GetEnv@L95348`, `rule.24.BuiltinSym-System-Run@L95408`
- `def.24.BuiltinSymJudg@L95425`, `def.24.StringBytesBuiltinMethodSets@L95438`, `def.24.StringBuiltinSymbols@L95454`, `def.24.BytesBuiltinSymbols@L95474`, `rule.24.BuiltinSym-String-Err@L95496`, `rule.24.BuiltinSym-Bytes-Err@L95512`, `def.24.DropHookJudg@L95528`, `rule.24.StringDropSym-Decl@L95541`
- `rule.24.BytesDropSym-Decl@L95556`, `rule.24.StringDropSym-Err@L95571`, `rule.24.BytesDropSym-Err@L95587`, `def.24.RuntimeDeclJudg@L95605`, `def.24.RuntimeMethodAndSymbolSets@L95618`, `def.24.CapabilityBuiltinSigs@L95637`, `def.24.CoreRuntimeSigs@L95655`, `def.24.BuiltinModalProcSigs@L95671`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95689`, `def.24.LLVMDeclType@L95708`, `rule.24.RuntimeDecls@L95721`, `def.24.RuntimeDeclarationCoverage@L95737`, `rule.24.Prim-Network-RestrictHost-Runtime@L95756`, `def.24.HeapJudg@L95772`, `req.24.HeapHostPrimitiveRelations@L95785`, `def.24.HeapStateAccountingDefinitions@L95798`
- `req.24.HeapPrimitiveSemantics@L95813`, `rule.24.Prim-Heap-WithQuota@L95839`, `rule.24.Prim-Heap-AllocRaw@L95855`, `rule.24.Prim-Heap-DeallocRaw@L95871`, `def.24.ReactorJudg@L95887`, `req.24.ReactorHostPrimitiveRelations@L95900`, `rule.24.Prim-Reactor-Run@L95913`, `rule.24.Prim-Reactor-Register@L95929`

#### `spec.backend`

Count: 190 total; 190 required; 0 recommended; 0 informative. Ledger line span: L95536-L98637.

- `def.24.LLVMHeader@L95951`, `def.24.OpaquePointerModel@L95966`, `def.24.LLVMAttrJudg@L95988`, `rule.24.PtrStateOf-Perm@L96001`, `rule.24.LLVM-PtrAttrs-Valid@L96017`, `rule.24.LLVM-PtrAttrs-Other@L96033`, `rule.24.LLVM-PtrAttrs-RawPtr@L96049`, `rule.24.LLVM-ArgAttrs-Ptr@L96065`
- `rule.24.LLVM-ArgAttrs-RawPtr@L96082`, `rule.24.LLVM-ArgAttrs-NonPtr@L96098`, `def.24.LLVMOptionalArgumentAttrs@L96114`, `def.24.LLVMUBAndPoisonAvoidance@L96132`, `def.24.MemoryIntrinsics@L96159`, `def.24.LLVMToolchain@L96182`, `req.24.HostedCompilerLLVMVersion@L96195`, `def.24.LLVMTyJudg@L96210`
- `def.24.LLVMPrimitiveTypeHelpers@L96223`, `def.24.StructElems@L96259`, `def.24.TaggedElems@L96277`, `rule.24.LLVMTy-Prim@L96294`, `rule.24.LLVMTy-Perm@L96310`, `rule.24.LLVMTy-Refine@L96326`, `rule.24.LLVMTy-Ptr@L96342`, `rule.24.LLVMTy-RawPtr@L96358`
- `rule.24.LLVMTy-Func@L96374`, `rule.24.LLVMTy-Closure@L96390`, `rule.24.LLVMTy-Alias@L96406`, `rule.24.LLVMTy-Record@L96422`, `rule.24.LLVMTy-Tuple@L96438`, `rule.24.LLVMTy-Array@L96454`, `rule.24.LLVMTy-Slice@L96470`, `rule.24.LLVMTy-Range@L96486`
- `rule.24.LLVMTy-RangeInclusive@L96502`, `rule.24.LLVMTy-RangeFrom@L96518`, `rule.24.LLVMTy-RangeTo@L96534`, `rule.24.LLVMTy-RangeToInclusive@L96550`, `rule.24.LLVMTy-RangeFull@L96566`, `rule.24.LLVMTy-Enum@L96581`, `rule.24.LLVMTy-Union-Niche@L96597`, `rule.24.LLVMTy-Union-Tagged@L96613`
- `rule.24.LLVMTy-Modal-Niche@L96629`, `rule.24.LLVMTy-Modal-Tagged@L96645`, `rule.24.LLVMTy-Modal-StringBytes@L96661`, `rule.24.LLVMTy-ModalState@L96679`, `rule.24.LLVMTy-Dynamic@L96695`, `rule.24.LLVMTy-StringView@L96711`, `rule.24.LLVMTy-StringManaged@L96727`, `rule.24.LLVMTy-BytesView@L96743`
- `rule.24.LLVMTy-BytesManaged@L96759`, `rule.24.LLVMTy-Err@L96775`, `def.24.LowerIRJudg@L96793`, `def.24.LLVMInstrHelpers@L96806`, `rule.24.LowerIRInstr-Empty@L96837`, `rule.24.LowerIRInstr-Seq@L96852`, `def.24.MemoryInstructionHelpers@L96868`, `def.24.ConstBytesEncoding@L96885`
- `def.24.StaticTypeBySymbol@L96911`, `def.24.StateRefJudg@L96925`, `rule.24.StateRef-Session@L96939`, `rule.24.StateRef-Global@L96955`, `def.24.CallSignatureHelpers@L96971`, `def.24.ParamInitHelpers@L96990`, `rule.24.LowerIRDecl-Proc-User@L97014`, `rule.24.LowerIRDecl-Proc-Gen@L97030`
- `rule.24.LowerIRDecl-GlobalConst@L97046`, `rule.24.LowerIRDecl-GlobalZero@L97062`, `req.24.HostedStateInitializerTemplates@L97078`, `rule.24.LowerIRDecl-VTable@L97091`, `rule.24.Lower-AllocIR@L97107`, `rule.24.Lower-BindVarIR@L97123`, `rule.24.Lower-ReadVarIR@L97139`, `rule.24.Lower-ReadVarIR-Err@L97155`
- `def.24.ProcSymbol@L97171`, `rule.24.Lower-ReadPathIR-Static-User@L97184`, `rule.24.Lower-ReadPathIR-Static-Gen@L97200`, `rule.24.Lower-ReadPathIR-Proc-User@L97216`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97232`, `rule.24.Lower-ReadPathIR-Runtime@L97248`, `rule.24.Lower-ReadPathIR-Record@L97264`, `rule.24.Lower-StoreVarIR@L97280`
- `rule.24.Lower-StoreVarNoDropIR@L97296`, `rule.24.Lower-MoveStateIR@L97312`, `rule.24.Lower-StoreGlobal@L97328`, `rule.24.Lower-ReadPlaceIR@L97344`, `rule.24.Lower-WritePlaceIR@L97360`, `def.24.PtrType@L97376`, `rule.24.Lower-ReadPtrIR@L97389`, `rule.24.Lower-ReadPtrIR-Raw@L97405`
- `rule.24.Lower-ReadPtrIR-Null@L97421`, `rule.24.Lower-ReadPtrIR-Expired@L97437`, `rule.24.Lower-WritePtrIR@L97453`, `rule.24.Lower-WritePtrIR-Null@L97469`, `rule.24.Lower-WritePtrIR-Expired@L97485`, `rule.24.Lower-WritePtrIR-Raw@L97501`, `rule.24.Lower-WritePtrIR-Raw-Err@L97517`, `rule.24.Lower-AddrOfIR@L97533`
- `def.24.CallLoweringHelpers@L97549`, `rule.24.Lower-CallIR-Func@L97577`, `def.24.DynamicDispatchHelpers@L97593`, `rule.24.Lower-CallVTable@L97611`, `rule.24.LowerIRInstr-ClearPanic@L97627`, `rule.24.LowerIRInstr-PanicCheck@L97643`, `rule.24.LowerIRInstr-CheckPoison@L97659`, `rule.24.LowerIRInstr-LowerPanic@L97675`
- `def.24.IfLoweringHelpers@L97691`, `rule.24.Lower-IfIR@L97708`, `def.24.BlockCleanupLoweringHelpers@L97724`, `rule.24.Lower-BlockIR@L97740`, `def.24.StructuredIRLoweringForms@L97756`, `rule.24.Lower-LoopIR@L97784`, `rule.24.Lower-IfCaseIR@L97800`, `rule.24.Lower-RegionIR@L97816`
- `rule.24.Lower-FrameIR@L97832`, `def.24.BranchLowerForms@L97848`, `rule.24.Lower-BranchIR-Unconditional@L97862`, `rule.24.Lower-BranchIR-Conditional@L97878`, `def.24.PhiLowerForm@L97893`, `rule.24.Lower-PhiIR@L97906`, `rule.24.LowerIRDecl-Err@L97922`, `rule.24.LowerIRInstr-Err@L97938`
- `def.24.BindStorageJudg@L97956`, `def.24.BindRegionTarget@L97979`, `req.24.ResolveTargetNearestLiveAlias@L97998`, `rule.24.BindValid-Sigma@L98011`, `rule.24.BindSlot-Param-ByValue@L98027`, `rule.24.BindSlot-Param-ByRef@L98043`, `rule.24.BindSlot-Region@L98059`, `rule.24.BindSlot-Local@L98075`
- `rule.24.BindSlot-Static@L98091`, `rule.24.UpdateValid-BindVar@L98107`, `rule.24.UpdateValid-StoreVar@L98122`, `rule.24.UpdateValid-StoreVarNoDrop@L98137`, `rule.24.UpdateValid-MoveRoot@L98153`, `rule.24.UpdateValid-PartialMove-Init@L98169`, `rule.24.UpdateValid-PartialMove-Step@L98185`, `def.24.DropOnAssignHelpers@L98201`
- `rule.24.DropOnAssign-NotApplicable@L98217`, `rule.24.DropOnAssign-Record-Valid@L98233`, `rule.24.DropOnAssign-Record-Partial@L98249`, `rule.24.DropOnAssign-Record-Moved@L98265`, `rule.24.DropOnAssign-Aggregate-Ok@L98281`, `rule.24.DropOnAssign-Aggregate-Moved@L98297`, `rule.24.BindSlot-Err@L98313`, `rule.24.BindValid-Err@L98329`
- `rule.24.UpdateValid-Err@L98345`, `rule.24.DropOnAssign-Err@L98361`, `def.24.LLVMCallJudg@L98379`, `def.24.LLVMCallSigFields@L98392`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98408`, `rule.24.LLVMArgLower-ByValue-Other@L98424`, `rule.24.LLVMArgLower-ByRef@L98440`, `rule.24.LLVMRetLower-ByValue-ZST@L98456`
- `rule.24.LLVMRetLower-ByValue@L98472`, `rule.24.LLVMRetLower-SRet@L98488`, `def.24.LLVMCallArgLists@L98504`, `rule.24.LLVMCall-ByValue@L98519`, `rule.24.LLVMCall-SRet@L98535`, `def.24.ByRefAccess@L98551`, `rule.24.LLVMArgLower-Err@L98566`, `rule.24.LLVMRetLower-Err@L98582`
- `rule.24.LLVMCall-Err@L98598`, `def.24.VTableJudg@L98616`, `def.24.VTableEmissionHelpers@L98629`, `rule.24.EmitDropGlue-Decl@L98653`, `rule.24.EmitVTable-Err@L98669`, `def.24.LiteralEmitJudg@L98687`, `def.24.StringBytesAndRawBytes@L98700`, `rule.24.EmitLiteralData-Decl@L98723`
- `rule.24.EmitLiteral-String@L98739`, `req.24.EmitLiteral-String-Utf8Valid@L98755`, `rule.24.EmitLiteral-Bytes@L98768`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98784`, `rule.24.EmitLiteral-Char@L98797`, `rule.24.EmitLiteral-Int@L98813`, `rule.24.EmitLiteral-Float@L98829`, `rule.24.EmitLiteral-Err@L98845`
- `def.24.PoisonJudg@L98863`, `def.24.PoisonSet@L98876`, `rule.24.PoisonFlag-Decl@L98889`, `def.24.PoisonFlagStorage@L98904`, `req.24.HostedPoisonFlagTemplate@L98918`, `rule.24.CheckPoison-Use@L98931`, `sem.24.CheckPoisonBehavior@L98947`, `req.24.HostedPoisonStateIsolation@L98960`
- `rule.24.SetPoison-OnInitFail@L98973`, `rule.24.PoisonFlag-Err@L98989`, `rule.24.CheckPoison-Err@L99005`, `rule.24.SetPoison-Err@L99021`, `req.24.OutputBackendDiagnosticsOwnership@L99039`, `diag.24.OutputBackendDiagnostics@L99052`
- `def.24.LLVMHeader@L95951`, `def.24.OpaquePointerModel@L95966`, `def.24.LLVMAttrJudg@L95988`, `rule.24.PtrStateOf-Perm@L96001`, `rule.24.LLVM-PtrAttrs-Valid@L96017`, `rule.24.LLVM-PtrAttrs-Other@L96033`, `rule.24.LLVM-PtrAttrs-RawPtr@L96049`, `rule.24.LLVM-ArgAttrs-Ptr@L96065`
- `rule.24.LLVM-ArgAttrs-RawPtr@L96082`, `rule.24.LLVM-ArgAttrs-NonPtr@L96098`, `def.24.LLVMOptionalArgumentAttrs@L96114`, `def.24.LLVMUBAndPoisonAvoidance@L96132`, `def.24.MemoryIntrinsics@L96159`, `def.24.LLVMToolchain@L96182`, `req.24.HostedCompilerLLVMVersion@L96195`, `def.24.LLVMTyJudg@L96210`
- `def.24.LLVMPrimitiveTypeHelpers@L96223`, `def.24.StructElems@L96259`, `def.24.TaggedElems@L96277`, `rule.24.LLVMTy-Prim@L96294`, `rule.24.LLVMTy-Perm@L96310`, `rule.24.LLVMTy-Refine@L96326`, `rule.24.LLVMTy-Ptr@L96342`, `rule.24.LLVMTy-RawPtr@L96358`
- `rule.24.LLVMTy-Func@L96374`, `rule.24.LLVMTy-Closure@L96390`, `rule.24.LLVMTy-Alias@L96406`, `rule.24.LLVMTy-Record@L96422`, `rule.24.LLVMTy-Tuple@L96438`, `rule.24.LLVMTy-Array@L96454`, `rule.24.LLVMTy-Slice@L96470`, `rule.24.LLVMTy-Range@L96486`
- `rule.24.LLVMTy-RangeInclusive@L96502`, `rule.24.LLVMTy-RangeFrom@L96518`, `rule.24.LLVMTy-RangeTo@L96534`, `rule.24.LLVMTy-RangeToInclusive@L96550`, `rule.24.LLVMTy-RangeFull@L96566`, `rule.24.LLVMTy-Enum@L96581`, `rule.24.LLVMTy-Union-Niche@L96597`, `rule.24.LLVMTy-Union-Tagged@L96613`
- `rule.24.LLVMTy-Modal-Niche@L96629`, `rule.24.LLVMTy-Modal-Tagged@L96645`, `rule.24.LLVMTy-Modal-StringBytes@L96661`, `rule.24.LLVMTy-ModalState@L96679`, `rule.24.LLVMTy-Dynamic@L96695`, `rule.24.LLVMTy-StringView@L96711`, `rule.24.LLVMTy-StringManaged@L96727`, `rule.24.LLVMTy-BytesView@L96743`
- `rule.24.LLVMTy-BytesManaged@L96759`, `rule.24.LLVMTy-Err@L96775`, `def.24.LowerIRJudg@L96793`, `def.24.LLVMInstrHelpers@L96806`, `rule.24.LowerIRInstr-Empty@L96837`, `rule.24.LowerIRInstr-Seq@L96852`, `def.24.MemoryInstructionHelpers@L96868`, `def.24.ConstBytesEncoding@L96885`
- `def.24.StaticTypeBySymbol@L96911`, `def.24.StateRefJudg@L96925`, `rule.24.StateRef-Session@L96939`, `rule.24.StateRef-Global@L96955`, `def.24.CallSignatureHelpers@L96971`, `def.24.ParamInitHelpers@L96990`, `rule.24.LowerIRDecl-Proc-User@L97014`, `rule.24.LowerIRDecl-Proc-Gen@L97030`
- `rule.24.LowerIRDecl-GlobalConst@L97046`, `rule.24.LowerIRDecl-GlobalZero@L97062`, `req.24.HostedStateInitializerTemplates@L97078`, `rule.24.LowerIRDecl-VTable@L97091`, `rule.24.Lower-AllocIR@L97107`, `rule.24.Lower-BindVarIR@L97123`, `rule.24.Lower-ReadVarIR@L97139`, `rule.24.Lower-ReadVarIR-Err@L97155`
- `def.24.ProcSymbol@L97171`, `rule.24.Lower-ReadPathIR-Static-User@L97184`, `rule.24.Lower-ReadPathIR-Static-Gen@L97200`, `rule.24.Lower-ReadPathIR-Proc-User@L97216`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97232`, `rule.24.Lower-ReadPathIR-Runtime@L97248`, `rule.24.Lower-ReadPathIR-Record@L97264`, `rule.24.Lower-StoreVarIR@L97280`
- `rule.24.Lower-StoreVarNoDropIR@L97296`, `rule.24.Lower-MoveStateIR@L97312`, `rule.24.Lower-StoreGlobal@L97328`, `rule.24.Lower-ReadPlaceIR@L97344`, `rule.24.Lower-WritePlaceIR@L97360`, `def.24.PtrType@L97376`, `rule.24.Lower-ReadPtrIR@L97389`, `rule.24.Lower-ReadPtrIR-Raw@L97405`
- `rule.24.Lower-ReadPtrIR-Null@L97421`, `rule.24.Lower-ReadPtrIR-Expired@L97437`, `rule.24.Lower-WritePtrIR@L97453`, `rule.24.Lower-WritePtrIR-Null@L97469`, `rule.24.Lower-WritePtrIR-Expired@L97485`, `rule.24.Lower-WritePtrIR-Raw@L97501`, `rule.24.Lower-WritePtrIR-Raw-Err@L97517`, `rule.24.Lower-AddrOfIR@L97533`
- `def.24.CallLoweringHelpers@L97549`, `rule.24.Lower-CallIR-Func@L97577`, `def.24.DynamicDispatchHelpers@L97593`, `rule.24.Lower-CallVTable@L97611`, `rule.24.LowerIRInstr-ClearPanic@L97627`, `rule.24.LowerIRInstr-PanicCheck@L97643`, `rule.24.LowerIRInstr-CheckPoison@L97659`, `rule.24.LowerIRInstr-LowerPanic@L97675`
- `def.24.IfLoweringHelpers@L97691`, `rule.24.Lower-IfIR@L97708`, `def.24.BlockCleanupLoweringHelpers@L97724`, `rule.24.Lower-BlockIR@L97740`, `def.24.StructuredIRLoweringForms@L97756`, `rule.24.Lower-LoopIR@L97784`, `rule.24.Lower-IfCaseIR@L97800`, `rule.24.Lower-RegionIR@L97816`
- `rule.24.Lower-FrameIR@L97832`, `def.24.BranchLowerForms@L97848`, `rule.24.Lower-BranchIR-Unconditional@L97862`, `rule.24.Lower-BranchIR-Conditional@L97878`, `def.24.PhiLowerForm@L97893`, `rule.24.Lower-PhiIR@L97906`, `rule.24.LowerIRDecl-Err@L97922`, `rule.24.LowerIRInstr-Err@L97938`
- `def.24.BindStorageJudg@L97956`, `def.24.BindRegionTarget@L97979`, `req.24.ResolveTargetNearestLiveAlias@L97998`, `rule.24.BindValid-Sigma@L98011`, `rule.24.BindSlot-Param-ByValue@L98027`, `rule.24.BindSlot-Param-ByRef@L98043`, `rule.24.BindSlot-Region@L98059`, `rule.24.BindSlot-Local@L98075`
- `rule.24.BindSlot-Static@L98091`, `rule.24.UpdateValid-BindVar@L98107`, `rule.24.UpdateValid-StoreVar@L98122`, `rule.24.UpdateValid-StoreVarNoDrop@L98137`, `rule.24.UpdateValid-MoveRoot@L98153`, `rule.24.UpdateValid-PartialMove-Init@L98169`, `rule.24.UpdateValid-PartialMove-Step@L98185`, `def.24.DropOnAssignHelpers@L98201`
- `rule.24.DropOnAssign-NotApplicable@L98217`, `rule.24.DropOnAssign-Record-Valid@L98233`, `rule.24.DropOnAssign-Record-Partial@L98249`, `rule.24.DropOnAssign-Record-Moved@L98265`, `rule.24.DropOnAssign-Aggregate-Ok@L98281`, `rule.24.DropOnAssign-Aggregate-Moved@L98297`, `rule.24.BindSlot-Err@L98313`, `rule.24.BindValid-Err@L98329`
- `rule.24.UpdateValid-Err@L98345`, `rule.24.DropOnAssign-Err@L98361`, `def.24.LLVMCallJudg@L98379`, `def.24.LLVMCallSigFields@L98392`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98408`, `rule.24.LLVMArgLower-ByValue-Other@L98424`, `rule.24.LLVMArgLower-ByRef@L98440`, `rule.24.LLVMRetLower-ByValue-ZST@L98456`
- `rule.24.LLVMRetLower-ByValue@L98472`, `rule.24.LLVMRetLower-SRet@L98488`, `def.24.LLVMCallArgLists@L98504`, `rule.24.LLVMCall-ByValue@L98519`, `rule.24.LLVMCall-SRet@L98535`, `def.24.ByRefAccess@L98551`, `rule.24.LLVMArgLower-Err@L98566`, `rule.24.LLVMRetLower-Err@L98582`
- `rule.24.LLVMCall-Err@L98598`, `def.24.VTableJudg@L98616`, `def.24.VTableEmissionHelpers@L98629`, `rule.24.EmitDropGlue-Decl@L98653`, `rule.24.EmitVTable-Err@L98669`, `def.24.LiteralEmitJudg@L98687`, `def.24.StringBytesAndRawBytes@L98700`, `rule.24.EmitLiteralData-Decl@L98723`
- `rule.24.EmitLiteral-String@L98739`, `req.24.EmitLiteral-String-Utf8Valid@L98755`, `rule.24.EmitLiteral-Bytes@L98768`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98784`, `rule.24.EmitLiteral-Char@L98797`, `rule.24.EmitLiteral-Int@L98813`, `rule.24.EmitLiteral-Float@L98829`, `rule.24.EmitLiteral-Err@L98845`
- `def.24.PoisonJudg@L98863`, `def.24.PoisonSet@L98876`, `rule.24.PoisonFlag-Decl@L98889`, `def.24.PoisonFlagStorage@L98904`, `req.24.HostedPoisonFlagTemplate@L98918`, `rule.24.CheckPoison-Use@L98931`, `sem.24.CheckPoisonBehavior@L98947`, `req.24.HostedPoisonStateIsolation@L98960`
- `rule.24.SetPoison-OnInitFail@L98973`, `rule.24.PoisonFlag-Err@L98989`, `rule.24.CheckPoison-Err@L99005`, `rule.24.SetPoison-Err@L99021`, `req.24.OutputBackendDiagnosticsOwnership@L99039`, `diag.24.OutputBackendDiagnostics@L99052`

### Lowering, Backend, Runtime Interface, And Driver

#### `backend.llvm-target`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L5343-L5373.

- `def.LLVMTargetConstants@L5343`, `def.IsRootModule@L5359`, `def.WithEntry@L5373`

#### `backend.llvm-codegen`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L5389-L6747.

- `CodegenObj-LLVM@L5389`, `CodegenIR-LLVM@L5407`, `AssembleIR-Ok@L6729`, `AssembleIR-Err@L6747`

#### `lowering.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27099-L27308.

- `conformance.AttributeLoweringOwnership@L27155`, `conformance.VendorAttributeLowering@L27364`
- `conformance.AttributeLoweringOwnership@L27155`, `conformance.VendorAttributeLowering@L27364`

#### `lowering.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27595-L27595.

- `conformance.LayoutAttributeLowering@L27651`
- `conformance.LayoutAttributeLowering@L27651`

#### `lowering.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27740-L27740.

- `conformance.OptimizationAttributeLowering@L27796`
- `conformance.OptimizationAttributeLowering@L27796`

#### `lowering.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28221-L28221.

- `conformance.DiagnosticsMetadataLowering@L28277`
- `conformance.DiagnosticsMetadataLowering@L28277`

#### `lowering.permissions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L28471-L28686.

- `conformance.PermissionLayoutNeutrality@L28812`, `req.PermissionFormsLoweringDiagnostics@L28874`, `conformance.AliasExclusivityLowering@L29027`
- `conformance.PermissionLayoutNeutrality@L28812`, `req.PermissionFormsLoweringDiagnostics@L28874`, `conformance.AliasExclusivityLowering@L29027`

#### `codegen`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L29113-L40918.

- `conformance.PermissionAdmissibilityLowering@L29454`, `conformance.ImportDeclarationLowering@L29689`, `conformance.UsingDeclarationLowering@L30196`, `def.ConstInitJudgementSet@L30489`, `def.ConstInitLiteralEncoding@L30505`, `def.StaticName@L30519`, `def.StaticBindingFunctionSignature@L30563`, `def.StaticSym@L30577`
- `def.InitSym@L30647`, `def.DeinitSym@L30679`, `def.StaticSymPath@L30739`, `def.StaticAddr@L30753`, `def.AddrOfSym@L30781`, `def.SeqIRList@L30823`, `def.StaticStoreIR@L30838`, `def.Rev@L30942`
- `conformance.ExternBlockLowering@L31387`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34584`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34600`, `def.PrimitiveValueBits@L34931`, `req.PrimitiveLayoutAbiOwnership@L34951`, `def.TupleFields@L35375`, `def.TupleLayoutJudgementSet@L35686`, `def.TupleValueBits@L35791`
- `def.ArrayLen@L36480`, `def.ArrayValueBits@L36494`, `def.SliceValueBits@L37049`, `def.LoweringChecksJudgementSet@L37711`, `def.RangeValueBits@L37727`, `def.RecordLayoutHelpers@L38887`, `def.FieldOffset@L38999`, `def.FieldValueList@L39013`
- `def.StructBits@L39027`, `def.PadBytes@L39041`, `def.RecordValueBits@L39055`, `def.EnumLayoutHelpers@L40065`, `def.EnumPayloadBits@L40166`, `def.EnumValueBits@L40182`, `def.UnionNicheOrderingHelpers@L40535`, `def.UnionTypeOrderingKeys@L40555`
- `def.TypeKey@L40597`, `def.TypeKeyOrdering@L40630`, `def.UnionMemberLayoutSelection@L40652`, `def.UnionLayoutHelpers@L40672`, `def.UnionNicheBits@L40781`, `def.UnionPayloadBits@L40795`, `def.TaggedBits@L40809`, `def.UnionTaggedBits@L40825`
- `def.UnionBits@L40839`, `def.UnionValueBits@L40853`, `def.TypeAliasValueBits@L41259`
- `conformance.PermissionAdmissibilityLowering@L29454`, `conformance.ImportDeclarationLowering@L29689`, `conformance.UsingDeclarationLowering@L30196`, `def.ConstInitJudgementSet@L30489`, `def.ConstInitLiteralEncoding@L30505`, `def.StaticName@L30519`, `def.StaticBindingFunctionSignature@L30563`, `def.StaticSym@L30577`
- `def.InitSym@L30647`, `def.DeinitSym@L30679`, `def.StaticSymPath@L30739`, `def.StaticAddr@L30753`, `def.AddrOfSym@L30781`, `def.SeqIRList@L30823`, `def.StaticStoreIR@L30838`, `def.Rev@L30942`
- `conformance.ExternBlockLowering@L31387`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34584`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34600`, `def.PrimitiveValueBits@L34931`, `req.PrimitiveLayoutAbiOwnership@L34951`, `def.TupleFields@L35375`, `def.TupleLayoutJudgementSet@L35686`, `def.TupleValueBits@L35791`
- `def.ArrayLen@L36480`, `def.ArrayValueBits@L36494`, `def.SliceValueBits@L37049`, `def.LoweringChecksJudgementSet@L37711`, `def.RangeValueBits@L37727`, `def.RecordLayoutHelpers@L38887`, `def.FieldOffset@L38999`, `def.FieldValueList@L39013`
- `def.StructBits@L39027`, `def.PadBytes@L39041`, `def.RecordValueBits@L39055`, `def.EnumLayoutHelpers@L40065`, `def.EnumPayloadBits@L40166`, `def.EnumValueBits@L40182`, `def.UnionNicheOrderingHelpers@L40535`, `def.UnionTypeOrderingKeys@L40555`
- `def.TypeKey@L40597`, `def.TypeKeyOrdering@L40630`, `def.UnionMemberLayoutSelection@L40652`, `def.UnionLayoutHelpers@L40672`, `def.UnionNicheBits@L40781`, `def.UnionPayloadBits@L40795`, `def.TaggedBits@L40809`, `def.UnionTaggedBits@L40825`
- `def.UnionBits@L40839`, `def.UnionValueBits@L40853`, `def.TypeAliasValueBits@L41259`
