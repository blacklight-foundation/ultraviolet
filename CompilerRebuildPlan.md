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
    AssemblyGraph.uv  [owns assembly dependency graph, assembly root ownership, owned modules, graph reachability, and assembly-project projection]
    AssemblyLoader.uv  [owns project loading lifecycle, assembly construction, assembly ownership failure propagation, and LoadProject state transitions]
    AssemblySelection.uv  [owns selected-assembly target resolution]
    BuildConfig.uv  [owns build manifest configuration and defaults]
    DeterministicOrder.uv  [owns deterministic file, directory, and compilation-unit ordering predicates]
    Manifest.uv  [owns parsed manifest records, manifest loading, manifest schema, and assembly field parsing]
    ManifestValidation.uv  [owns manifest table projection, key/type validation, deterministic validation ordering, first-failure selection, and project diagnostics]
    ModuleDiscovery.uv  [owns source root validation, module directory discovery, and compilation-unit derivation]
    OutputArtifacts.uv  [owns project output artifact and project.command-output relations]
    ProjectModel.uv  [owns project, assembly, module, source-file, kind, and lookup value records]
    RootDiscovery.uv  [owns command input path normalization and nearest-manifest project root search]
    ToolchainConfig.uv  [owns toolchain manifest configuration and selected target-profile values]
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
      Commands.uv  [owns command model, command result model, and dispatch vocabulary]
      InitCommand.uv
      OptionValues.uv  [owns CLI target profile and output mode value states]
      Options.uv  [owns command arguments and System invocation parsing]
      Positionals.uv  [owns CLI positional argument states]
      RunCommand.uv
      TestCommand.uv  [owns uv test command coordination and positional test target input routing]
      Text.uv  [owns byte-precise CLI text comparison]
      Version.uv  [owns version command display integration]
    ConformanceTrace.uv
    CrashReporting.uv
    Fingerprints.uv
    Incremental.uv
    Pipeline.uv  [owns translation pipeline ordering and CLI pipeline request boundary]
    Testing/
      TestCoverage.uv  [owns source-native coverage references and coverage checks]
      TestDiscovery.uv  [owns uv test target resolution and deterministic discovery order]
      TestExecution.uv  [owns source-native test execution lifecycle data]
      TestHarness.uv  [owns ephemeral harness planning and generation lifecycle]
      TestResults.uv  [owns source-native test outcomes, results, and summaries]
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
    Source/
      Parser/
        TestAttributes/
          GrammarTestAttributeTests.uv  [covers grammar.TestAttribute@L28336]
          ParseTestAttributeByOrdinaryAttributeParserTests.uv  [covers parse.TestAttributeByOrdinaryAttributeParser@L28356]
    Semantics/
      Attributes/
        TestAttributes/
          AstTestProcedureClassificationTests.uv  [covers ast.TestProcedureClassification@L28374]
          DefTestNameTests.uv  [covers def.TestName@L28389]
          DefTestCoverageTests.uv  [covers def.TestCoverage@L28404]
          ReqTestAttributeProcedureTargetTests.uv  [covers req.TestAttributeProcedureTarget@L28421]
          DefTestAttributeArgsOkTests.uv  [covers def.TestAttributeArgsOk@L28435]
      Declarations/
        TestProcedures/
          ReqTestProcedureShapeTests.uv  [covers req.TestProcedureShape@L28455]
          ReqTestContextAuthorityTests.uv  [covers req.TestContextAuthority@L28477]
    Driver/
      Testing/
        ConformanceTestAttributeDynamicSemanticsTests.uv  [covers conformance.TestAttributeDynamicSemantics@L28495]
        LoweringTestHarnessGenerationTests.uv  [covers lowering.TestHarnessGeneration@L28516]
        DefTestDiscoveryOrderTests.uv  [covers def.TestDiscoveryOrder@L28575]
    Diagnostics/
      TestAttributes/
        DiagnosticsTestAttributesTests.uv  [covers diagnostics.TestAttributes@L28593]
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
  `def.24.DefaultCallingConventionAndTargetArtifacts@L90854`.

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

Project records, manifests, assembly loading, module discovery, target profiles, command-output relations, and output artifact naming.

Obligation owners:
- `conformance.target-abi` (7 total, 7 required)
- `project.assembly-graph` (16 total, 16 required)
- `project.assembly-loader` (5 total, 5 required)
- `project.assembly-ownership` (5 total, 5 required)
- `project.assembly-selection` (4 total, 4 required)
- `project.build-config` (4 total, 4 required)
- `project.context` (1 total, 1 required)
- `project.core-records` (3 total, 3 required)
- `project.command-output` (6 total, 6 required)
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

Compilation pipeline ordering, CLI pipeline request submission, conformance traces, incremental fingerprints, crash reporting, source-native test discovery and execution, and test coverage reporting.

`Compiler/Driver/CLI` owns command parsing, command selection, command-specific entrypoints, command options, command-result failure data, and version display. `Compiler/Driver/Pipeline.uv` owns the typed request boundary used by project commands before project loading and execution are migrated. `Compiler/Driver/CLI/TestCommand.uv` owns CLI integration for `uv test`. `Compiler/Driver/Testing/TestDiscovery.uv` owns target resolution and deterministic discovery of `[[test]]` procedures. `Compiler/Driver/Testing/TestHarness.uv` owns generated harness construction and the primary `lowering.TestHarnessGeneration@L28516` obligation. `Compiler/Driver/Testing/TestExecution.uv` owns invocation and result classification for `conformance.TestAttributeDynamicSemantics@L28495`. `Compiler/Driver/Testing/TestResults.uv` supports result records and rendering. `Compiler/Driver/Testing/TestCoverage.uv` supports obligation-ledger coverage checks over coverage references classified by `Compiler/Semantics/Attributes/TestAttributes.uv`.

Executable command surface responsibilities:

- `Tools/Uv/Main.uv` owns the `uv` executable source entrypoint. It must define
  exactly one `public procedure main(context: Context) -> i32`, delegate to the
  driver CLI, and provide the source declaration consumed by
  `def.15.MainEntryPointDefinitions@L52806` and `rule.15.Main-Ok@L52827`.
  The main-check implementation for those obligations is owned by
  `Compiler/Semantics/Declarations/Procedures.uv`.
- `Compiler/Driver/CLI/Api.uv` owns `DriverHost`, the CLI authority projection
  record created from the `Context` received by `main`. It carries the
  filesystem output authority, normalized process argument authority,
  executable path, and current directory into command dispatch.
- `Compiler/Driver/CLI/Options.uv` owns command argument records and command
  parsing from `System::argument_count()` and `System::argument(index)`.
- `Compiler/Driver/CLI/Positionals.uv` owns positional argument state and the
  optional `uv test [target]` target text shape consumed by
  `lowering.TestHarnessGeneration@L28516`.
- `Compiler/Driver/CLI/OptionValues.uv` owns target-profile and output-mode
  option input states. It converts CLI option input into driver-owned
  `PipelineTargetProfile` and `PipelineOutputMode` values when project commands
  construct pipeline requests.
- `Compiler/Driver/CLI/Text.uv` owns byte-precise CLI text comparison used by
  command parsing and command-surface tests.
- `Compiler/Driver/CLI/Commands.uv` owns the command modal, command result
  modal, dispatch vocabulary, command failure data, and command-result
  conversion. Build, check, clean, init, run, test, and version states route
  execution to their command-specific files.
- `Compiler/Driver/Pipeline.uv` owns `PipelineCommand`,
  `PipelineTargetProfile`, `PipelineOutputMode`, `PipelineRequest`,
  `PipelineSubmission`, and `PipelineUnavailableReason`. Build, check, clean,
  init, and run command files construct typed pipeline requests and submit them
  through this boundary.
- `Compiler/Project/OutputArtifacts.uv` owns the project command-output
  obligations: `def.DumpProjectOutput@L2073`,
  `def.ProjectSummaryOutput@L2090`, `def.OutputSummary@L2104`,
  `def.LinkOutputSummary@L2118`, `def.IROpt@L2134`, and
  `def.ImportLibOpt@L2150`. The relation bodies are implemented with the
  project/output artifact migration slice.
- `Compiler/Driver/CLI/TestCommand.uv` owns `uv test` command coordination for
  the positional target model in `lowering.TestHarnessGeneration@L28516`. It
  routes target resolution and deterministic discovery to
  `Compiler/Driver/Testing/TestDiscovery.uv`, coverage validation to
  `Compiler/Driver/Testing/TestCoverage.uv`, harness generation to
  `Compiler/Driver/Testing/TestHarness.uv`, execution to
  `Compiler/Driver/Testing/TestExecution.uv`, and result reporting to
  `Compiler/Driver/Testing/TestResults.uv`.
- `Compiler/Driver/CLI/Version.uv` owns version command display text and its
  command-result integration.

Process invocation and System responsibility:

- `Compiler/Semantics/Capabilities/SystemCapabilities.uv` owns
  `def.14.SystemDecl@L51462` and the built-in `System` method signature
  surface.
- `Runtime/Host/Startup.uv` owns
  `def.24.ProcessInvocationNormalization@L93519`.
- `Runtime/Host/Platform.uv` owns
  `req.24.ProcessInvocationPlatformIsolation@L93538`.
- `Runtime/System/Environment.uv` owns `Prim-System-GetEnv@L14335`.
- `Runtime/System/Process.uv` owns `Prim-System-Exit@L14425`,
  `Prim-System-Run@L14443`, `Prim-System-ExecutablePath@L14353`,
  `Prim-System-ArgumentCount@L14371`, `Prim-System-Argument@L14389`, and
  `Prim-System-CurrentDirectory@L14407`.
- `Compiler/Backend/IR/RuntimeSymbols.uv` owns all `BuiltinSym-System-*`
  runtime symbol identity obligations.

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

`Compiler/Semantics/Attributes/TestAttributes.uv` owns source-native test classification, stable/display test naming, source-order coverage-reference extraction, test-attribute target checking, and test-attribute argument validation for `ast.TestProcedureClassification@L28374`, `def.TestName@L28389`, `def.TestCoverage@L28404`, `req.TestAttributeProcedureTarget@L28421`, and `def.TestAttributeArgsOk@L28435`. Procedure body, generic, visibility, return-type, postcondition, and `TestContext` parameter validation are owned by `Compiler/Semantics/Declarations/Procedures.uv`.

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
3. Map the object to obligation owners and exact obligation IDs from Appendix A.
4. Read the corresponding `SPECIFICATION.md` section before writing code.
5. Implement the general rule in the canonical module; delete or avoid any duplicate behavior path.
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

## Workstreams And Required Obligation Owners

Each workstream lists the exact obligation owners it must discharge. Appendix A lists the exact obligation IDs under each owner.

### Repository Scaffold And Style Normalization

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

- Attributes: `def.SpecAttributeRegistry@L26957`, `def.SpecAttributeTargets@L26980`, `def.AttributeStaticSemantics.Helpers2@L27084`.
- Test attributes: `grammar.TestAttribute@L28336`, `parse.TestAttributeByOrdinaryAttributeParser@L28356`, `ast.TestProcedureClassification@L28374`, `def.TestName@L28389`, `def.TestCoverage@L28404`, `req.TestAttributeProcedureTarget@L28421`, `def.TestAttributeArgsOk@L28435`, `req.TestProcedureShape@L28455`, `req.TestContextAuthority@L28477`, `conformance.TestAttributeDynamicSemantics@L28495`, `lowering.TestHarnessGeneration@L28516`, `def.TestDiscoveryOrder@L28575`, `diagnostics.TestAttributes@L28593`.
- Contracts: `grammar.15.Postconditions@L55223`, `def.15.PostconditionProofContext@L55305`, `rule.15.Post-Valid@L55320`, `req.15.ContractResultProperties@L55356`, `req.15.PostconditionResultRuntimeBinding@L55521`.
- Project model and parent assembly loading: `WF-Assembly-Kind@L2394`, `WF-Assembly@L3511`, `Select-By-Name@L3887`.

Compiler conformance tests must live in PascalCase `Tests` submodules inside the parent assembly root that owns the behavior. Test helper code lives in `Compiler/Tests/TestSupport`; parser tests live in `Compiler/Tests/Parser` and `Compiler/Tests/Source/Parser`; semantic tests live in `Compiler/Tests/Semantics`; driver tests live in `Compiler/Tests/Driver`; diagnostics tests live in `Compiler/Tests/Diagnostics`; checker tests live in `Compiler/Tests/Checker`; lowering and backend tests live in `Compiler/Tests/Lowering` and `Compiler/Tests/Backend`; runtime tests live in `Runtime/Tests`; bootstrap fixed-point tests live in `Tools/Uv/Tests/Bootstrap`.

The source-native test-surface obligations each have a dedicated owning test
file:

- `grammar.TestAttribute@L28336`: `Compiler/Tests/Source/Parser/TestAttributes/GrammarTestAttributeTests.uv`.
- `parse.TestAttributeByOrdinaryAttributeParser@L28356`: `Compiler/Tests/Source/Parser/TestAttributes/ParseTestAttributeByOrdinaryAttributeParserTests.uv`.
- `ast.TestProcedureClassification@L28374`: `Compiler/Tests/Semantics/Attributes/TestAttributes/AstTestProcedureClassificationTests.uv`.
- `def.TestName@L28389`: `Compiler/Tests/Semantics/Attributes/TestAttributes/DefTestNameTests.uv`.
- `def.TestCoverage@L28404`: `Compiler/Tests/Semantics/Attributes/TestAttributes/DefTestCoverageTests.uv`.
- `req.TestAttributeProcedureTarget@L28421`: `Compiler/Tests/Semantics/Attributes/TestAttributes/ReqTestAttributeProcedureTargetTests.uv`.
- `def.TestAttributeArgsOk@L28435`: `Compiler/Tests/Semantics/Attributes/TestAttributes/DefTestAttributeArgsOkTests.uv`.
- `req.TestProcedureShape@L28455`: `Compiler/Tests/Semantics/Declarations/TestProcedures/ReqTestProcedureShapeTests.uv`.
- `req.TestContextAuthority@L28477`: `Compiler/Tests/Semantics/Declarations/TestProcedures/ReqTestContextAuthorityTests.uv`.
- `conformance.TestAttributeDynamicSemantics@L28495`: `Compiler/Tests/Driver/Testing/ConformanceTestAttributeDynamicSemanticsTests.uv`.
- `lowering.TestHarnessGeneration@L28516`: `Compiler/Tests/Driver/Testing/LoweringTestHarnessGenerationTests.uv`.
- `def.TestDiscoveryOrder@L28575`: `Compiler/Tests/Driver/Testing/DefTestDiscoveryOrderTests.uv`.
- `diagnostics.TestAttributes@L28593`: `Compiler/Tests/Diagnostics/TestAttributes/DiagnosticsTestAttributesTests.uv`.

The project-loading and deterministic-ordering slice uses obligation-specific
test files under `Compiler/Tests/Project`:

- `req.ProjectRootResolveInputPath@L2281`: `Compiler/Tests/Project/RootDiscovery/ReqProjectRootResolveInputPath/ReqProjectRootResolveInputPathTests.uv`.
- `req.ProjectRootFileInputStartsAtParent@L2296`: `Compiler/Tests/Project/RootDiscovery/ReqProjectRootFileInputStartsAtParent/ReqProjectRootFileInputStartsAtParentTests.uv`.
- `req.ProjectRootDirectoryInputStartsAtResolvedPath@L2310`: `Compiler/Tests/Project/RootDiscovery/ReqProjectRootDirectoryInputStartsAtResolvedPath/ReqProjectRootDirectoryInputStartsAtResolvedPathTests.uv`.
- `def.FindProjectRoot@L2324`: `Compiler/Tests/Project/RootDiscovery/DefFindProjectRoot/DefFindProjectRootTests.uv`.
- `def.ManifestParsing@L2168`: `Compiler/Tests/Project/Manifest/DefManifestParsing/DefManifestParsingTests.uv`.
- `Parse-Manifest-Ok@L2183`: `Compiler/Tests/Project/Manifest/ParseManifestOk/ParseManifestOkTests.uv`.
- `Parse-Manifest-Err@L2219`: `Compiler/Tests/Project/Manifest/ParseManifestErr/ParseManifestErrTests.uv`.
- `WF-Project-Root@L3491`: `Compiler/Tests/Project/RootDiscovery/WFProjectRoot/WFProjectRootTests.uv`.
- `WF-TopKeys@L2589`: `Compiler/Tests/Project/ManifestValidation/WFTopKeys/WFTopKeysTests.uv`.
- `WF-TopKeys-Err@L2607`: `Compiler/Tests/Project/ManifestValidation/WFTopKeysErr/WFTopKeysErrTests.uv`.
- `WF-Assembly-Keys@L2748`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyKeys/WFAssemblyKeysTests.uv`.
- `WF-Assembly-Keys-Err@L2766`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyKeysErr/WFAssemblyKeysErrTests.uv`.
- `WF-Assembly-Required-Types@L2784`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyRequiredTypes/WFAssemblyRequiredTypesTests.uv`.
- `WF-Assembly-Required-Types-Err@L2802`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyRequiredTypesErr/WFAssemblyRequiredTypesErrTests.uv`.
- `WF-Assembly-Name@L2358`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyName/WFAssemblyNameTests.uv`.
- `WF-Assembly-Name-Err@L2376`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyNameErr/WFAssemblyNameErrTests.uv`.
- `WF-Assembly-Kind@L2394`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyKind/WFAssemblyKindTests.uv`.
- `WF-Assembly-Kind-Err@L2412`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyKindErr/WFAssemblyKindErrTests.uv`.
- `WF-Assembly-Root-Path@L2430`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyRootPath/WFAssemblyRootPathTests.uv`.
- `WF-Assembly-Root-Path-Err@L2448`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyRootPathErr/WFAssemblyRootPathErrTests.uv`.
- `WF-Assembly-OutDir-Path@L2466`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyOutDirPath/WFAssemblyOutDirPathTests.uv`.
- `WF-Assembly-OutDir-Path-Err@L2484`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyOutDirPathErr/WFAssemblyOutDirPathErrTests.uv`.
- `WF-Assembly-EmitIR@L2502`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyEmitIR/WFAssemblyEmitIRTests.uv`.
- `WF-Assembly-EmitIR-Err@L2520`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyEmitIRErr/WFAssemblyEmitIRErrTests.uv`.
- `WF-Assembly-LinkKind@L2926`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyLinkKind/WFAssemblyLinkKindTests.uv`.
- `WF-Assembly-LinkKind-Err@L2944`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyLinkKindErr/WFAssemblyLinkKindErrTests.uv`.
- `WF-Assembly-LinkKind-Use-Err@L2962`: `Compiler/Tests/Project/ManifestValidation/WFAssemblyLinkKindUseErr/WFAssemblyLinkKindUseErrTests.uv`.
- `Step-Parse@L3558`: `Compiler/Tests/Project/AssemblyLoader/StepParse/StepParseTests.uv`.
- `Step-Parse-Err@L3576`: `Compiler/Tests/Project/AssemblyLoader/StepParseErr/StepParseErrTests.uv`.
- `Step-Validate@L3594`: `Compiler/Tests/Project/AssemblyLoader/StepValidate/StepValidateTests.uv`.
- `Step-Validate-Err@L3612`: `Compiler/Tests/Project/AssemblyLoader/StepValidateErr/StepValidateErrTests.uv`.
- `Step-Asm-Done@L3795`: `Compiler/Tests/Project/AssemblyLoader/StepAsmDone/StepAsmDoneTests.uv`.
- `Step-Asm-Done-Err@L3831`: `Compiler/Tests/Project/AssemblyLoader/StepAsmDoneErr/StepAsmDoneErrTests.uv`.
- `BuildAssembly-Ok@L3925`: `Compiler/Tests/Project/AssemblyLoader/BuildAssemblyOk/BuildAssemblyOkTests.uv`.
- `LoadProject-Err@L4017`: `Compiler/Tests/Project/AssemblyLoader/LoadProjectErr/LoadProjectErrTests.uv`.
- `Select-Only@L3851`: `Compiler/Tests/Project/AssemblySelection/SelectOnly/SelectOnlyTests.uv`.
- `Select-Only-Exe@L3869`: `Compiler/Tests/Project/AssemblySelection/SelectOnlyExe/SelectOnlyExeTests.uv`.
- `Select-By-Name@L3887`: `Compiler/Tests/Project/AssemblySelection/SelectByName/SelectByNameTests.uv`.
- `Select-Err@L3905`: `Compiler/Tests/Project/AssemblySelection/SelectErr/SelectErrTests.uv`.
- `def.FoldPath@L4095`: `Compiler/Tests/Project/DeterministicOrder/FoldPath/DefFoldPathTests.uv`.
- `def.FileKey@L4109`: `Compiler/Tests/Project/DeterministicOrder/FileKey/DefFileKeyTests.uv`.
- `def.FileOrdering@L4125`: `Compiler/Tests/Project/DeterministicOrder/FileOrdering/DefFileOrderingTests.uv`.
- `def.Fold@L4157`: `Compiler/Tests/Project/DeterministicOrder/Fold/DefFoldTests.uv`.
- `def.DirKey@L4172`: `Compiler/Tests/Project/DeterministicOrder/DirKey/DefDirKeyTests.uv`.
- `def.DirectoryOrdering@L4188`: `Compiler/Tests/Project/DeterministicOrder/DirectoryOrdering/DefDirectoryOrderingTests.uv`.
- `def.SourceRootDirectories@L4254`: `Compiler/Tests/Project/ModuleDiscovery/DefSourceRootDirectories/DefSourceRootDirectoriesTests.uv`.
- `WF-Source-Root@L4270`: `Compiler/Tests/Project/ModuleDiscovery/WFSourceRoot/WFSourceRootTests.uv`.
- `WF-Source-Root-Err@L4288`: `Compiler/Tests/Project/ModuleDiscovery/WFSourceRootErr/WFSourceRootErrTests.uv`.
- `Module-Dir@L4306`: `Compiler/Tests/Project/ModuleDiscovery/ModuleDir/ModuleDirTests.uv`.
- `def.ModuleDirectoryFiles@L4324`: `Compiler/Tests/Project/ModuleDiscovery/DefModuleDirectoryFiles/DefModuleDirectoryFilesTests.uv`.
- `def.CompilationUnits@L4338`: `Compiler/Tests/Project/ModuleDiscovery/DefCompilationUnits/DefCompilationUnitsTests.uv`.
- `CompilationUnit-Rel-Fail@L4352`: `Compiler/Tests/Project/ModuleDiscovery/CompilationUnitRelFail/CompilationUnitRelFailTests.uv`.
- `refs.ModuleDiscoverySection@L4370`: `Compiler/Tests/Project/ModuleDiscovery/RefsModuleDiscoverySection/RefsModuleDiscoverySectionTests.uv`.
- `Modules-Ok@L4384`: `Compiler/Tests/Project/ModuleDiscovery/ModulesOk/ModulesOkTests.uv`.
- `Modules-Err@L4402`: `Compiler/Tests/Project/ModuleDiscovery/ModulesErr/ModulesErrTests.uv`.
- `def.AssemblySourceRoots@L4420`: `Compiler/Tests/Project/AssemblyGraph/DefAssemblySourceRoots/DefAssemblySourceRootsTests.uv`.
- `def.OwnerRoot@L4435`: `Compiler/Tests/Project/AssemblyGraph/DefOwnerRoot/DefOwnerRootTests.uv`.
- `def.OwnedModules@L4449`: `Compiler/Tests/Project/AssemblyGraph/DefOwnedModules/DefOwnedModulesTests.uv`.
- `OwnAssemblies-Ok@L4463`: `Compiler/Tests/Project/AssemblyGraph/OwnAssembliesOk/OwnAssembliesOkTests.uv`.
- `WF-Assembly-Root-Owner-Ambiguous@L4481`: `Compiler/Tests/Project/AssemblyGraph/WFAssemblyRootOwnerAmbiguous/WFAssemblyRootOwnerAmbiguousTests.uv`.

`LoadProject-Ok@L3999` closure is the project-loader end-to-end acceptance
point for parsed manifest input, manifest validation, module discovery, assembly
ownership, assembly selection, and final project construction.

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
    covers("AttrList-Unknown@L27048"),
    covers("diagnostics.AttributeDiagnostics@L27189")
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

- `diagnostics.RuntimeStateAndMemoryDiagnostics@L18438`

#### `diagnostics.name-resolution`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L23703-L23703.

- `diagnostics.NameResolutionAndReservedNames@L23775`

#### `diagnostics.types`

Count: 2 total; 1 required; 0 recommended; 0 informative. Ledger line span: L26206-L40949.

- `diagnostics.CoreTypeDiagnostics@L26278`, `diagnostics.DataTypesSupplement@L41308`
- `diagnostics.CoreTypeDiagnostics@L26278`, `diagnostics.DataTypesSupplement@L41308`

#### `diagnostics.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27115-L27324.

- `diagnostics.AttributeDiagnostics@L27189`, `diagnostics.VendorAttributeDiagnostics@L27398`
- `diagnostics.AttributeDiagnostics@L27189`, `diagnostics.VendorAttributeDiagnostics@L27398`

#### `diagnostics.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27611-L27611.

- `diagnostics.LayoutAttributeDiagnostics@L27685`
- `diagnostics.LayoutAttributeDiagnostics@L27685`

#### `diagnostics.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27756-L27756.

- `diagnostics.OptimizationAttributeDiagnostics@L27830`
- `diagnostics.OptimizationAttributeDiagnostics@L27830`

#### `diagnostics.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28237-L28237.

- `diagnostics.DiagnosticsMetadataAttributes@L28311`
- `diagnostics.DiagnosticsMetadataAttributes@L28311`

#### `diagnostics.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28549-L28702.

- `req.PermissionFormsDiagnosticOwnership@L28908`, `req.AliasExclusivityDiagnosticOwnership@L29061`
- `req.PermissionFormsDiagnosticOwnership@L28908`, `req.AliasExclusivityDiagnosticOwnership@L29061`

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

- `grammar.AttributeSyntaxAndPlacement@L26306`, `req.AttributeReservedLeafNames@L26337`, `req.AttributeImmediateTargetPlacement@L26351`, `Parse-AttrListOpt-None@L26367`, `Parse-AttrListOpt-Yes@L26385`, `Parse-AttrList-Cons@L26403`, `Parse-AttrListTail-End@L26421`, `Parse-AttrListTail-Cons@L26439`
- `Parse-AttrBlock@L26457`, `Parse-AttrSpecList-Cons@L26475`, `Parse-AttrSpecListTail-End@L26493`, `Parse-AttrSpecListTail-TrailingComma@L26511`, `Parse-AttrSpecListTail-Comma@L26529`, `Parse-AttrSpec@L26547`, `Parse-AttrArgsOpt-None@L26565`, `Parse-AttrArgsOpt-Yes@L26583`
- `Parse-AttrArgList-Cons@L26601`, `Parse-AttrArgListTail-End@L26619`, `Parse-AttrArgListTail-TrailingComma@L26637`, `Parse-AttrArgListTail-Comma@L26655`, `Parse-AttrArg-Named-Literal@L26673`, `Parse-AttrArg-Named-Ident@L26691`, `Parse-AttrArg-Named-Call@L26709`, `Parse-AttrArg-Literal@L26727`
- `Parse-AttrArg-Ident@L26745`, `def.AttributeAstRepresentation@L26766`, `def.AttributeVendorPrefixAst@L26785`, `def.AttributeArgumentAst@L26799`, `def.AttributeSpecAst@L26813`, `def.AttributeListAst@L26827`, `def.ExpressionAttributes@L26842`, `def.AttachExpressionAttributes@L26856`
- `def.ItemAttributeList@L26870`, `def.AttributeByName@L26885`, `conformance.VendorAttributeSyntaxReuse@L27209`, `req.VendorAttributeParserReuse@L27227`, `def.AttributeLeafToken@L27241`, `Parse-AttrName-Plain@L27255`, `Parse-AttrName-Vendor@L27273`, `Parse-VendorPrefixTail-End@L27291`
- `Parse-VendorPrefixTail-Cons@L27309`, `def.VendorAttributeAst@L27330`
- `def.ItemAttributeList@L26870`, `def.AttributeByName@L26885`, `conformance.VendorAttributeSyntaxReuse@L27209`, `req.VendorAttributeParserReuse@L27227`, `def.AttributeLeafToken@L27241`, `Parse-AttrName-Plain@L27255`, `Parse-AttrName-Vendor@L27273`, `Parse-VendorPrefixTail-End@L27291`
- `Parse-VendorPrefixTail-Cons@L27309`, `def.VendorAttributeAst@L27330`

#### `parser.attributes.layout`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27344-L27383.

- `grammar.LayoutAttributeSyntax@L27418`, `req.LayoutAttributeParserReuse@L27441`, `def.LayoutAttributeAstAttachment@L27457`
- `grammar.LayoutAttributeSyntax@L27418`, `req.LayoutAttributeParserReuse@L27441`, `def.LayoutAttributeAstAttachment@L27457`

#### `parser.attributes.optimization`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L27633-L27672.

- `grammar.OptimizationAttributeSyntax@L27707`, `req.OptimizationAttributeParserReuse@L27730`, `def.OptimizationAttributeAstAttachment@L27746`
- `grammar.OptimizationAttributeSyntax@L27707`, `req.OptimizationAttributeParserReuse@L27730`, `def.OptimizationAttributeAstAttachment@L27746`

#### `parser.attributes.metadata`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L27774-L27823.

- `req.DiagnosticsMetadataSyntaxParsingAst@L27848`, `req.DiagnosticsMetadataParserReuse@L27866`, `def.ExpressionAttributeList@L27882`, `def.ExpressionAttributeByName@L27897`
- `req.DiagnosticsMetadataSyntaxParsingAst@L27848`, `req.DiagnosticsMetadataParserReuse@L27866`, `def.ExpressionAttributeList@L27882`, `def.ExpressionAttributeByName@L27897`

#### `parser.permissions`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L28262-L28752.

- `grammar.PermissionFormsSyntax@L28621`, `req.PermissionQualifierTypeGrammarPlacement@L28640`, `req.PermissionParserOwnership@L28656`, `req.ParseReceiverCanonicalOwner@L28670`, `def.PermissionAstForms@L28686`, `def.PermissionQualifiedTypeAst@L28704`, `req.AliasExclusivityNoParsingRules@L28943`, `req.AliasExclusivityNoAstForms@L28959`
- `req.BindingActivityNoParsingRules@L29095`, `req.BindingActivityNoAstNode@L29111`
- `grammar.PermissionFormsSyntax@L28621`, `req.PermissionQualifierTypeGrammarPlacement@L28640`, `req.PermissionParserOwnership@L28656`, `req.ParseReceiverCanonicalOwner@L28670`, `def.PermissionAstForms@L28686`, `def.PermissionQualifiedTypeAst@L28704`, `req.AliasExclusivityNoParsingRules@L28943`, `req.AliasExclusivityNoAstForms@L28959`
- `req.BindingActivityNoParsingRules@L29095`, `req.BindingActivityNoAstNode@L29111`

#### `parser`

Count: 7 total; 7 required; 0 recommended; 0 informative. Ledger line span: L28918-L30777.

- `req.PermissionAdmissibilityNoAdditionalParsing@L29277`, `grammar.ImportDeclarationSyntax@L29514`, `req.ImportDeclarationParserBranch@L29534`, `grammar.UsingDeclarationSyntax@L29758`, `grammar.StaticDeclarationSyntax@L30268`, `req.StaticDeclParserOwnership@L30301`, `grammar.ExternBlockShellSyntax@L31136`
- `req.PermissionAdmissibilityNoAdditionalParsing@L29277`, `grammar.ImportDeclarationSyntax@L29514`, `req.ImportDeclarationParserBranch@L29534`, `grammar.UsingDeclarationSyntax@L29758`, `grammar.StaticDeclarationSyntax@L30268`, `req.StaticDeclParserOwnership@L30301`, `grammar.ExternBlockShellSyntax@L31136`

#### `parser.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L30797-L30797.

- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31156`
- `req.ExternProcedureDeclSyntaxOwnedByFfiChapter@L31156`

#### `parser.modules`

Count: 58 total; 58 required; 0 recommended; 0 informative. Ledger line span: L31081-L32075.

- `grammar.ModulePathSyntax@L31440`, `req.ModuleToFileMappingNoSurfaceSyntax@L31459`, `req.ModulePathParserOwnership@L31473`, `Parse-ModulePath@L31489`, `Parse-ModulePathTail-End@L31507`, `Parse-ModulePathTail-Cons@L31525`, `def.PathAstAliases@L31543`, `def.ASTModule@L31564`
- `def.ASTFile@L31581`, `Module-Path-Root@L31617`, `Module-Path-Rel@L31635`, `Module-Path-Rel-Fail@L31653`, `def.ModuleDirOf@L31671`, `def.ProjectModuleView@L31686`, `def.SourceRootOfModule@L31704`, `def.WFModulePathJudgementSet@L31718`
- `WF-Module-Path-Ok@L31732`, `WF-Module-Path-Reserved@L31750`, `WF-Module-Path-Ident-Err@L31768`, `WF-Module-Path-Collision@L31786`, `def.ModuleAggregationInputsOutputs@L31804`, `def.ModuleMap@L31821`, `def.ASTModuleOfProjectPath@L31835`, `def.PathOfModule@L31849`
- `def.ParseModuleRuleReference@L31879`, `def.ParseModuleBigStepInputs@L31893`, `ReadBytes-Ok@L31910`, `ReadBytes-Err@L31928`, `def.BytesOfFile@L31946`, `ParseModule-Ok@L31960`, `ParseModule-Err-Read@L31978`, `ParseModule-Err-Load@L31996`
- `req.LoadSourceShortCircuit@L32014`, `ParseModule-Err-Unit@L32029`, `ParseModule-Err-Parse@L32047`, `def.ParseFileBestEffort@L32065`, `def.ParseFileOk@L32079`, `def.ParseFileDiag@L32093`, `def.HasErrorDiagnostics@L32107`, `def.ModState@L32121`
- `Mod-Start@L32136`, `Mod-Start-Err-Unit@L32154`, `Mod-Scan@L32172`, `Mod-Scan-Err-Read@L32190`, `Mod-Scan-Err-Load@L32208`, `Mod-Scan-Err-Parse@L32226`, `Mod-Done@L32244`, `def.ParseModulesBigStepInputs@L32261`
- `ParseModules-Ok@L32277`, `ParseModules-Err@L32295`, `def.DiscState@L32313`, `Disc-Start@L32327`, `Disc-Skip@L32344`, `Disc-Add@L32362`, `Disc-Collision@L32380`, `Disc-Invalid-Component@L32398`
- `Disc-Rel-Fail@L32416`, `Disc-Done@L32434`
- `grammar.ModulePathSyntax@L31440`, `req.ModuleToFileMappingNoSurfaceSyntax@L31459`, `req.ModulePathParserOwnership@L31473`, `Parse-ModulePath@L31489`, `Parse-ModulePathTail-End@L31507`, `Parse-ModulePathTail-Cons@L31525`, `def.PathAstAliases@L31543`, `def.ASTModule@L31564`
- `def.ASTFile@L31581`, `Module-Path-Root@L31617`, `Module-Path-Rel@L31635`, `Module-Path-Rel-Fail@L31653`, `def.ModuleDirOf@L31671`, `def.ProjectModuleView@L31686`, `def.SourceRootOfModule@L31704`, `def.WFModulePathJudgementSet@L31718`
- `WF-Module-Path-Ok@L31732`, `WF-Module-Path-Reserved@L31750`, `WF-Module-Path-Ident-Err@L31768`, `WF-Module-Path-Collision@L31786`, `def.ModuleAggregationInputsOutputs@L31804`, `def.ModuleMap@L31821`, `def.ASTModuleOfProjectPath@L31835`, `def.PathOfModule@L31849`
- `def.ParseModuleRuleReference@L31879`, `def.ParseModuleBigStepInputs@L31893`, `ReadBytes-Ok@L31910`, `ReadBytes-Err@L31928`, `def.BytesOfFile@L31946`, `ParseModule-Ok@L31960`, `ParseModule-Err-Read@L31978`, `ParseModule-Err-Load@L31996`
- `req.LoadSourceShortCircuit@L32014`, `ParseModule-Err-Unit@L32029`, `ParseModule-Err-Parse@L32047`, `def.ParseFileBestEffort@L32065`, `def.ParseFileOk@L32079`, `def.ParseFileDiag@L32093`, `def.HasErrorDiagnostics@L32107`, `def.ModState@L32121`
- `Mod-Start@L32136`, `Mod-Start-Err-Unit@L32154`, `Mod-Scan@L32172`, `Mod-Scan-Err-Read@L32190`, `Mod-Scan-Err-Load@L32208`, `Mod-Scan-Err-Parse@L32226`, `Mod-Done@L32244`, `def.ParseModulesBigStepInputs@L32261`
- `ParseModules-Ok@L32277`, `ParseModules-Err@L32295`, `def.DiscState@L32313`, `Disc-Start@L32327`, `Disc-Skip@L32344`, `Disc-Add@L32362`, `Disc-Collision@L32380`, `Disc-Invalid-Component@L32398`
- `Disc-Rel-Fail@L32416`, `Disc-Done@L32434`

#### `parser.types`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L34302-L40557.

- `grammar.PrimitiveTypeSyntax@L34661`, `def.PrimLexemeSet@L34688`, `grammar.TupleSyntax@L35002`, `req.TupleSingletonCommaIllFormed@L35026`, `def.TupleScanDepthAndStep@L35138`, `def.TupleScanPredicates@L35154`, `grammar.ArraySyntax@L35865`, `grammar.SliceSyntax@L36563`
- `grammar.RangeSyntax@L37132`, `req.RangeTypeParserOwnership@L37156`, `grammar.RecordSyntax@L38209`, `grammar.EnumSyntax@L39184`, `req.EnumVariantSeparatorSyntax@L39209`, `req.EnumTopLevelCommaSeparatorRejected@L39441`, `grammar.UnionTypeSyntax@L40294`, `grammar.TypeAliasSyntax@L40916`
- `grammar.PrimitiveTypeSyntax@L34661`, `def.PrimLexemeSet@L34688`, `grammar.TupleSyntax@L35002`, `req.TupleSingletonCommaIllFormed@L35026`, `def.TupleScanDepthAndStep@L35138`, `def.TupleScanPredicates@L35154`, `grammar.ArraySyntax@L35865`, `grammar.SliceSyntax@L36563`
- `grammar.RangeSyntax@L37132`, `req.RangeTypeParserOwnership@L37156`, `grammar.RecordSyntax@L38209`, `grammar.EnumSyntax@L39184`, `req.EnumVariantSeparatorSyntax@L39209`, `req.EnumTopLevelCommaSeparatorRejected@L39441`, `grammar.UnionTypeSyntax@L40294`, `grammar.TypeAliasSyntax@L40916`

#### `spec.grammar`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L98736-L99400.

- `grammar.B.1.LexicalGrammar@L99222`, `grammar.B.2.TypeGrammar@L99272`, `req.B.2.ClosureTypeUnionParameterParentheses@L99332`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99345`, `grammar.B.3.ExpressionGrammar@L99378`, `req.B.3.ClosureExprUnionParameterParentheses@L99456`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99469`, `grammar.B.4.PatternGrammar@L99503`
- `grammar.B.5.StatementGrammar@L99533`, `grammar.B.6.DeclarationGrammar@L99570`, `grammar.B.7.ContractGrammar@L99654`, `grammar.B.8.AttributeGrammar@L99681`, `grammar.B.9.KeySystemGrammar@L99737`, `grammar.B.10.ConcurrencyGrammar@L99767`, `grammar.B.11.AsyncGrammar@L99801`, `grammar.B.12.MetaprogrammingGrammar@L99830`
- `grammar.B.13.FFIGrammar@L99863`, `grammar.B.14.RegionGrammar@L99889`
- `grammar.B.1.LexicalGrammar@L99222`, `grammar.B.2.TypeGrammar@L99272`, `req.B.2.ClosureTypeUnionParameterParentheses@L99332`, `grammar.B.2.GenericRefinementModalTypeGrammar@L99345`, `grammar.B.3.ExpressionGrammar@L99378`, `req.B.3.ClosureExprUnionParameterParentheses@L99456`, `grammar.B.3.ControlAndSpecialExpressionGrammar@L99469`, `grammar.B.4.PatternGrammar@L99503`
- `grammar.B.5.StatementGrammar@L99533`, `grammar.B.6.DeclarationGrammar@L99570`, `grammar.B.7.ContractGrammar@L99654`, `grammar.B.8.AttributeGrammar@L99681`, `grammar.B.9.KeySystemGrammar@L99737`, `grammar.B.10.ConcurrencyGrammar@L99767`, `grammar.B.11.AsyncGrammar@L99801`, `grammar.B.12.MetaprogrammingGrammar@L99830`
- `grammar.B.13.FFIGrammar@L99863`, `grammar.B.14.RegionGrammar@L99889`

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
- `Prim-File-Flush-Append@L14227`, `Prim-File-Close-Read@L14245`, `Prim-File-Close-Write@L14263`, `Prim-File-Close-Append@L14281`, `Prim-Dir-Next@L14299`, `Prim-Dir-Close@L14317`, `Prim-System-GetEnv@L14335`, `Prim-System-Exit@L14425`
- `Prim-System-Run@L14443`, `Prim-Network-RestrictHost@L14461`, `req.PrimSystemExitAbortOutcome@L14479`

#### `runtime.binding-store`

Count: 35 total; 35 required; 0 recommended; 0 informative. Ledger line span: L16858-L17386.

- `def.ScopeEntry@L16930`, `def.DynamicScopeStack@L16949`, `def.UpdateScopeStack@L16967`, `def.RuntimeScopePushPop@L16981`, `def.AppendCleanup@L16996`, `def.CleanupList@L17010`, `def.ScopeById@L17024`, `def.ReplaceScopeById@L17041`
- `def.SetCleanupList@L17058`, `def.PoisonedModule@L17072`, `req.HostedSessionPoisonFlagLocalization@L17086`, `def.PoisonedModules@L17100`, `def.RuntimeBindingIdentityAndValue@L17114`, `def.FreshBindId@L17129`, `def.Last@L17143`, `def.NearestScope@L17158`
- `def.LookupBind@L17175`, `def.RuntimeBindingValueLookup@L17189`, `def.RuntimeBindingStateLookup@L17203`, `LookupVal-Bind-Value@L17217`, `LookupVal-Bind-Alias@L17235`, `LookupVal-Path@L17253`, `LookupValPath-Builtin@L17271`, `LookupValPath-Static@L17289`
- `LookupValPath-Proc@L17307`, `LookupValPath-RecordCtor@L17325`, `def.ScopeValueAndStateUpdate@L17343`, `def.UpdateRuntimeBindingValue@L17358`, `def.SetRuntimeBindingState@L17372`, `def.RuntimeBindingTypeAndInfo@L17386`, `def.BindRuntimeValue@L17401`, `def.BindPatternValue@L17415`
- `def.PatternBindingOrder@L17429`, `def.BindRuntimeList@L17443`, `def.BindPattern@L17458`

#### `runtime.regions`

Count: 45 total; 45 required; 0 recommended; 0 informative. Ledger line span: L17402-L18064.

- `def.RuntimeRegionEntry@L17474`, `def.RuntimeAddressTags@L17492`, `def.RuntimeRegionStack@L17507`, `def.RegionArena@L17521`, `def.UpdateRegionArena@L17536`, `def.ArenaNew@L17550`, `def.FreshRuntimeAddress@L17564`, `def.Prefix@L17578`
- `def.ArenaAppend@L17592`, `def.ArenaMark@L17606`, `def.ArenaResetTo@L17620`, `def.ArenaClear@L17634`, `def.ArenaRemove@L17648`, `def.RegionValue@L17662`, `def.ResolveRuntimeRegionEntry@L17677`, `def.ActiveRuntimeRegion@L17694`
- `def.ResolveRuntimeRegionTargetAndTag@L17709`, `def.FreshRuntimeRegionTagAndArena@L17724`, `def.UpdateRegionStack@L17739`, `def.RegionNew@L17753`, `def.RegionOpen@L17767`, `def.FrameEnter@L17781`, `def.BindRegionAlias@L17795`, `def.TagAddr@L17810`
- `def.TagAddrFrom@L17824`, `def.RegionAlloc@L17838`, `def.FreshRuntimeRegionTags@L17852`, `def.RetagRegions@L17866`, `def.RegionReset@L17883`, `def.PopRegions@L17897`, `def.RegionFree@L17914`, `def.FrameMark@L17928`
- `def.PopRegionScope@L17942`, `def.ReleaseArena@L17959`, `def.ResetArena@L17973`, `req.RegionRuntimeOwnershipBoundary@L17987`, `req.RegionReleaseCleanupBeforeArenaReclaim@L18003`, `req.ArenaReclaimNoDrop@L18018`, `def.RegionProcedureJudgements@L18032`, `Region-New-Scoped@L18046`
- `Region-Alloc-Proc@L18064`, `Region-Reset-Proc@L18082`, `Region-Freeze-Proc@L18100`, `Region-Thaw-Proc@L18118`, `Region-Free-Proc@L18136`

#### `runtime.value-model`

Count: 17 total; 17 required; 0 recommended; 0 informative. Ledger line span: L18082-L18347.

- `def.BlockEnter@L18154`, `def.ScalarRuntimeValues@L18168`, `def.PointerRuntimeValues@L18190`, `def.AggregateRuntimeValues@L18204`, `def.RuntimeValueDomain@L18227`, `def.TupleValueOperations@L18241`, `def.RecordFieldValueOperations@L18256`, `def.IndexAndSliceValueOperations@L18271`
- `def.AddressPrimitiveJudgments@L18289`, `def.AddressArithmetic@L18303`, `def.ElementType@L18317`, `def.AggregateAddressCalculation@L18331`, `def.PointerStateAndAddress@L18350`, `def.BindingAddresses@L18369`, `def.RuntimeAddressTagLookup@L18387`, `def.RuntimeTagActive@L18404`
- `def.DynamicAddressState@L18419`

#### `runtime.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27083-L27292.

- `conformance.AttributeDynamicSemantics@L27157`, `conformance.VendorAttributeDynamicSemantics@L27366`
- `conformance.AttributeDynamicSemantics@L27157`, `conformance.VendorAttributeDynamicSemantics@L27366`

#### `runtime.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27579-L27579.

- `conformance.LayoutAttributeDynamicSemantics@L27653`
- `conformance.LayoutAttributeDynamicSemantics@L27653`

#### `runtime.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27724-L27724.

- `conformance.OptimizationAttributeDynamicSemantics@L27798`
- `conformance.OptimizationAttributeDynamicSemantics@L27798`

#### `runtime.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28203-L28203.

- `conformance.DiagnosticsMetadataDynamicSemantics@L28277`
- `conformance.DiagnosticsMetadataDynamicSemantics@L28277`

#### `runtime.permissions`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L28451-L28670.

- `conformance.PermissionDynamicSemantics@L28810`, `conformance.AliasExclusivityDynamicSemantics@L29029`
- `conformance.PermissionDynamicSemantics@L28810`, `conformance.AliasExclusivityDynamicSemantics@L29029`

#### `runtime`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L29083-L40526.

- `conformance.PermissionAdmissibilityRuntimeIdentity@L29442`, `conformance.ImportDeclarationDynamicSemantics@L29691`, `conformance.UsingDeclarationDynamicSemantics@L30198`, `conformance.StaticDeclarationDynamicSemantics@L30491`, `req.HostedLibraryStaticAddrSessionLocal@L30785`, `conformance.ExternBlockDynamicSemantics@L31389`, `conformance.ModuleAggregationDynamicSemantics@L34586`, `def.PrimitiveValueTypes@L34912`
- `req.PrimitiveOperationEvaluationOwnership@L34935`, `def.TupleValueType@L35616`, `def.ArrayValueType@L36337`, `def.ArrayIndexRuntimeHelpers@L36353`, `def.SliceValueType@L36947`, `def.SliceRuntimeIndexHelpers@L36963`, `def.SliceIndexUpdate@L37081`, `def.RangeValueTypes@L37606`
- `def.SliceBoundsRaw@L37695`, `def.SliceBounds@L37714`, `def.RecordValueType@L38803`, `def.RecordDefaultInits@L38855`, `def.EnumValueType@L39978`, `def.UnionCase@L40536`, `def.UnionValueType@L40885`
- `conformance.PermissionAdmissibilityRuntimeIdentity@L29442`, `conformance.ImportDeclarationDynamicSemantics@L29691`, `conformance.UsingDeclarationDynamicSemantics@L30198`, `conformance.StaticDeclarationDynamicSemantics@L30491`, `req.HostedLibraryStaticAddrSessionLocal@L30785`, `conformance.ExternBlockDynamicSemantics@L31389`, `conformance.ModuleAggregationDynamicSemantics@L34586`, `def.PrimitiveValueTypes@L34912`
- `req.PrimitiveOperationEvaluationOwnership@L34935`, `def.TupleValueType@L35616`, `def.ArrayValueType@L36337`, `def.ArrayIndexRuntimeHelpers@L36353`, `def.SliceValueType@L36947`, `def.SliceRuntimeIndexHelpers@L36963`, `def.SliceIndexUpdate@L37081`, `def.RangeValueTypes@L37606`
- `def.SliceBoundsRaw@L37695`, `def.SliceBounds@L37714`, `def.RecordValueType@L38803`, `def.RecordDefaultInits@L38855`, `def.EnumValueType@L39978`, `def.UnionCase@L40536`, `def.UnionValueType@L40885`

### Modules, Name Resolution, And Visibility

#### `checker.name-resolution`

Count: 307 total; 307 required; 0 recommended; 0 informative. Ledger line span: L18399-L23682.

- `def.ScopeKeyConstraint@L18471`, `def.GlobalResolutionTables@L18486`, `def.ResolutionContext@L18503`, `def.ResolutionEntity@L18521`, `def.ResolutionScope@L18539`, `def.ResolutionScopeStack@L18553`, `def.UniverseBindings@L18571`, `def.BytePrefix@L18586`
- `def.ReservedIdentifiers@L18601`, `def.ReservedModulePath@L18617`, `def.BuiltinTypeNameSets@L18633`, `req.PredicateNameUniverseReservation@L18649`, `def.NameResolutionKeywordKeys@L18663`, `def.ScopeDomain@L18683`, `def.NameIntroductionScopeSequence@L18697`, `def.InScope@L18711`
- `def.InOuter@L18725`, `Intro-Ok@L18739`, `Intro-Dup@L18757`, `Intro-Outer-Err@L18775`, `Intro-Reserved-Gen-Err@L18793`, `Intro-Reserved-Ultraviolet-Err@L18811`, `req.IntroRulePriority@L18829`, `def.UsingAlias@L18845`
- `Using-Alias-Ok@L18861`, `Using-Alias-Unresolved@L18879`, `Using-Alias-Dup@L18897`, `Using-Alias-Reserved@L18915`, `req.UsingAliasRulePriority@L18933`, `def.ModuleNameValidationHelpers@L18947`, `Validate-Module-Ok@L18961`, `Validate-Module-Keyword-Err@L18979`
- `req.UniverseScopeReuseHandledByIntro@L18997`, `def.LookupScopeSequence@L19013`, `def.LookupNearestScopeIndex@L19027`, `Lookup-Unqualified@L19041`, `Lookup-Unqualified-None@L19059`, `def.EntityKindPredicates@L19077`, `def.RegionAliasName@L19095`, `Resolve-Value-Name@L19109`
- `Resolve-Type-Name@L19127`, `Resolve-Class-Name@L19145`, `Resolve-Module-Name@L19163`, `def.QualifiedResolutionProjectInput@L19181`, `def.QualifiedResolutionCurrentModule@L19195`, `def.QualifiedResolutionVisibleModules@L19209`, `def.QualifiedResolutionAliasMap@L19224`, `def.QualifiedResolutionKindDomain@L19238`
- `req.ModuleVisibilityJudgementOwnership@L19252`, `req.ResolveModulePathCanonicalOwner@L19266`, `Resolve-Qualified@L19280`, `prop.CollectNamesOrderIndependence@L19536`, `def.BindingKindDomain@L19550`, `def.BindingSourceDomain@L19564`, `def.NameInfoShape@L19578`, `def.ModuleNameMap@L19592`
- `def.AliasMap@L19606`, `def.UsingMap@L19620`, `def.UsingValueMap@L19634`, `def.UsingTypeMap@L19648`, `def.TypeMap@L19662`, `def.ClassMap@L19676`, `PatNames-IdentifierPattern@L19690`, `PatNames-WildcardPattern@L19705`
- `PatNames-LiteralPattern@L19720`, `PatNames-TuplePattern@L19735`, `PatNames-RecordFieldPattern@L19752`, `PatNames-RecordFieldShorthand@L19769`, `PatNames-RecordPattern@L19784`, `PatNames-EnumNoPayload@L19801`, `PatNames-EnumTuplePayload@L19816`, `PatNames-EnumRecordPayload@L19833`
- `PatNames-RangePattern@L19850`, `def.AllModuleNames@L19867`, `def.VisibleModuleNames@L19881`, `def.LastPathComponent@L19895`, `def.IsModulePath@L19909`, `def.SplitLastPathComponent@L19923`, `def.ModuleByPath@L32696`, `def.ItemNames@L32724`
- `def.UsingSpecName@L29989`, `def.UsingSpecNames@L30005`, `DeclNames-Empty@L19995`, `DeclNames-Using@L20010`, `DeclNames-Item@L20028`, `def.ModuleDeclNames@L20046`, `req.ImportUsingJudgementCanonicalOwner@L20060`, `Bind-Procedure@L20074`
- `Bind-ExternBlock@L31317`, `Bind-Record@L38515`, `Bind-Enum@L39524`, `Bind-Class@L20143`, `Bind-TypeAlias@L40990`, `Bind-Static@L30383`, `Bind-Import@L29638`, `Bind-Import-Err@L29656`
- `Bind-Using@L30145`, `Bind-Using-Err@L30163`, `Bind-ErrorItem@L20267`, `Collect-Ok@L20284`, `Collect-Scan@L20302`, `Collect-Using-Import-Dup@L20320`, `Collect-Dup@L20338`, `Collect-Err@L20356`
- `PatNames-RangePattern@L19850`, `def.AllModuleNames@L19867`, `def.VisibleModuleNames@L19881`, `def.LastPathComponent@L19895`, `def.IsModulePath@L19909`, `def.SplitLastPathComponent@L19923`, `def.ModuleByPath@L32696`, `def.ItemNames@L32724`
- `def.UsingSpecName@L29989`, `def.UsingSpecNames@L30005`, `DeclNames-Empty@L19995`, `DeclNames-Using@L20010`, `DeclNames-Item@L20028`, `def.ModuleDeclNames@L20046`, `req.ImportUsingJudgementCanonicalOwner@L20060`, `Bind-Procedure@L20074`
- `Bind-ExternBlock@L31317`, `Bind-Record@L38515`, `Bind-Enum@L39524`, `Bind-Class@L20143`, `Bind-TypeAlias@L40990`, `Bind-Static@L30383`, `Bind-Import@L29638`, `Bind-Import-Err@L29656`
- `Bind-Using@L30145`, `Bind-Using-Err@L30163`, `Bind-ErrorItem@L20267`, `Collect-Ok@L20284`, `Collect-Scan@L20302`, `Collect-Using-Import-Dup@L20320`, `Collect-Dup@L20338`, `Collect-Err@L20356`
- `def.BindingNameSet@L20374`, `def.NoDuplicateBindingNames@L20388`, `def.DisjointBindingNames@L20402`, `def.NameMapUnion@L20416`, `def.NameInfoOfBinding@L20430`, `def.BindingNameSource@L20444`, `def.NameMapSource@L20458`, `def.UsingImportConflict@L20472`
- `def.NameCollectionStateDomain@L20486`, `Names-Start@L20500`, `Names-Step@L20517`, `Names-Step-Using-Import-Dup@L20535`, `Names-Step-Dup@L20553`, `Names-Step-Err@L20571`, `Names-Done@L20589`, `def.ResolveQualifiedFormSignature@L20608`
- `def.ResolveArgsSignature@L20622`, `def.ResolveFieldInitsSignature@L20636`, `def.ResolveRecordPathSignature@L20650`, `def.ResolveEnumUnitSignature@L20664`, `def.ResolveEnumTupleSignature@L20678`, `def.ResolveEnumRecordSignature@L20692`, `def.ResolvePathJudgementSet@L20706`, `ResolveArgs-Empty@L20720`
- `ResolveArgs-Cons@L20737`, `ResolveFieldInits-Empty@L20755`, `ResolveFieldInits-Cons@L20772`, `Resolve-RecordPath@L38532`, `Resolve-EnumUnit@L39559`, `Resolve-EnumTuple@L39577`, `Resolve-EnumRecord@L39595`, `def.BuiltinValuePath@L20862`
- `ResolveQual-Name-Builtin@L20876`, `ResolveQual-Name-Value@L20894`, `ResolveQual-Name-Record@L38550`, `ResolveQual-Name-Enum@L39613`, `ResolveQual-Name-Err@L20948`, `def.SharedResolutionProjectInput@L20969`, `def.SharedResolutionCurrentModule@L20983`, `def.SharedResolutionAstModule@L20997`
- `ResolveArgs-Cons@L20737`, `ResolveFieldInits-Empty@L20755`, `ResolveFieldInits-Cons@L20772`, `Resolve-RecordPath@L38532`, `Resolve-EnumUnit@L39559`, `Resolve-EnumTuple@L39577`, `Resolve-EnumRecord@L39595`, `def.BuiltinValuePath@L20862`
- `ResolveQual-Name-Builtin@L20876`, `ResolveQual-Name-Value@L20894`, `ResolveQual-Name-Record@L38550`, `ResolveQual-Name-Enum@L39613`, `ResolveQual-Name-Err@L20948`, `def.SharedResolutionProjectInput@L20969`, `def.SharedResolutionCurrentModule@L20983`, `def.SharedResolutionAstModule@L20997`
- `def.SharedResolutionInputs@L21011`, `def.SharedResolutionOutputs@L21025`, `def.PathOfModuleReference@L21039`, `def.TypeParamBindings@L21053`, `ResolveGenericParamsOpt-None@L21068`, `ResolvePredicateClauseOpt-None@L21083`, `ResolveContractClauseOpt-None@L21098`, `ResolveInvariantOpt-None@L21113`
- `ResolveTypeOpt-None@L21128`, `ResolveExprOpt-None-Judgement@L21143`, `def.ResolveExprOptNone@L21158`, `def.ResolveExprOptSome@L21172`, `ResolveGenericParamsOpt-Yes@L21186`, `ResolveTypeParam@L21204`, `ResolveTypeParamList-Empty@L21222`, `ResolveTypeParamList-Cons@L21237`
- `ResolvePredicateClauseOpt-Yes@L21255`, `ResolvePredicateReqList-Empty@L21273`, `ResolvePredicateReq-Predicate@L21288`, `ResolvePredicateReqList-Cons@L21306`, `ResolveContractClauseOpt-Yes@L21324`, `ResolveInvariantOpt-Yes@L21342`, `ResolveTypePath-Ident@L21360`, `ResolveTypePath-Ident-Local@L21378`
- `ResolveTypePath-Qual@L21396`, `def.LocalTypePath@L21414`, `ResolveClassPath-Ident@L21428`, `ResolveClassPath-Qual@L21446`, `ResolveType-Path@L21464`, `ResolveType-Dynamic@L21482`, `ResolveType-Apply@L21500`, `ResolveType-ModalState@L21518`
- `def.ResolveModalRef@L21536`, `ResolveType-Hom@L21552`, `ResolveTypeList-Empty@L21570`, `ResolveTypeList-Cons@L21585`, `ResolveParam@L21603`, `ResolveParams-Empty@L21621`, `ResolveParams-Cons@L21636`, `def.ResolvePatternSignature@L21655`
- `ResolvePat-Wildcard@L21669`, `ResolvePat-Identifier@L21684`, `ResolvePat-Literal@L21699`, `ResolvePat-Tuple@L21714`, `ResolvePat-Record@L21732`, `ResolvePat-Enum@L21750`, `ResolvePat-Modal@L21768`, `ResolvePat-Range@L21786`
- `ResolvePatternList-Empty@L21804`, `ResolveFieldPatternList-Empty@L21819`, `ResolvePatternList-Cons@L21834`, `ResolveFieldPattern-Implicit@L21852`, `ResolveFieldPattern-Explicit@L21869`, `ResolveFieldPatternList-Cons@L21887`, `ResolveEnumPayloadPattern-None@L21905`, `ResolveEnumPayloadPattern-Tuple@L21920`
- `ResolveEnumPayloadPattern-Record@L21938`, `ResolveFieldPatternListOpt-None@L21956`, `ResolveFieldPatternListOpt-Some@L21971`, `ResolveExpr-Ident@L21990`, `ResolveExpr-Ident-Err@L22008`, `ResolveExpr-Qualified@L22026`, `def.ResolveArgsReference@L22044`, `def.ResolveFieldInitsReference@L22058`
- `ResolveExprList-Empty@L22072`, `ResolveExprList-Cons@L22087`, `def.ResolveExprListJudgementSet@L22105`, `def.ResolveEnumPayloadJudgementSet@L22119`, `ResolveEnumPayload-None@L22133`, `ResolveEnumPayload-Tuple@L22150`, `ResolveEnumPayload-Record@L22168`, `def.ResolveKeyPathJudgementSet@L22186`
- `ResolveKeySeg-Field@L22200`, `ResolveKeySeg-Index@L22218`, `ResolveKeySegs-Empty@L22236`, `ResolveKeySegs-Cons@L22253`, `ResolveKeyPathExpr@L22271`, `ResolveKeyPathExpr-Err@L22289`, `ResolveKeyPathList-Empty@L22307`, `ResolveKeyPathList-Cons@L22324`
- `def.ResolveParallelOptJudgementSet@L22342`, `ResolveParallelOpt-Cancel@L22356`, `ResolveParallelOpt-Name@L22374`, `ResolveParallelOpts-Empty@L22391`, `ResolveParallelOpts-Cons@L22408`, `def.ResolveSpawnOptJudgementSet@L22426`, `ResolveSpawnOpt-Name@L22440`, `ResolveSpawnOpt-Affinity@L22457`
- `ResolveSpawnOpt-Priority@L22475`, `ResolveSpawnOpts-Empty@L22493`, `ResolveSpawnOpts-Cons@L22510`, `def.ResolveDispatchOptJudgementSet@L22528`, `ResolveDispatchOpt-Reduce@L22542`, `ResolveDispatchOpt-Ordered@L22559`, `ResolveDispatchOpt-Chunk@L22576`, `ResolveDispatchOpts-Empty@L22594`
- `ResolveDispatchOpts-Cons@L22611`, `def.ResolveRaceJudgementSet@L22629`, `ResolveRaceHandler-Return@L22643`, `ResolveRaceHandler-Yield@L22661`, `ResolveRaceArm@L22679`, `ResolveRaceArms-Empty@L22697`, `ResolveRaceArms-Cons@L22714`, `def.ResolveAllExprListJudgementSet@L22732`
- `ResolveAllExprList-Empty@L22746`, `ResolveAllExprList-Cons@L22763`, `def.ResolveCalleeJudgementSet@L22781`, `ResolveCallee-Ident-Value@L22795`, `ResolveCallee-Ident-Record@L22813`, `ResolveCallee-Path-Value@L22831`, `ResolveCallee-Path-Builtin@L22849`, `ResolveCallee-Path-Record@L22867`
- `ResolveCallee-Other@L22885`, `ResolveExpr-Call@L22903`, `ResolveExpr-Call-TypeArgs@L22921`, `ResolveExpr-RecordExpr@L22940`, `ResolveExpr-EnumLiteral@L22958`, `def.ResolveIfCaseJudgementSet@L22976`, `ResolveIfCase@L22990`, `ResolveIfCases-Empty@L23008`
- `ResolveIfCases-Cons@L23025`, `ResolveElseBlockOpt-None@L23043`, `ResolveElseBlockOpt-Some@L23060`, `ResolveExpr-IfIs@L23078`, `ResolveExpr-IfCase@L23096`, `ResolveExpr-LoopInfinite@L23114`, `ResolveExpr-LoopConditional@L23132`, `ResolveExpr-LoopIter@L23150`
- `ResolveExpr-Parallel@L23168`, `ResolveExpr-Spawn@L23186`, `ResolveExpr-Wait@L23204`, `def.ResolveKeyClauseJudgementSet@L23222`, `ResolveKeyClauseOpt-None@L23236`, `ResolveKeyClauseOpt-Yes@L23254`, `ResolveExpr-Dispatch@L23272`, `ResolveExpr-Yield@L23290`
- `ResolveExpr-YieldFrom@L23308`, `ResolveExpr-Sync@L23326`, `ResolveExpr-Race@L23344`, `ResolveExpr-All@L23362`, `ResolveExpr-Alloc-Explicit-ByAlias@L23380`, `def.ResolveExprRuleSet@L23398`, `def.NoSpecificResolveExpr@L23412`, `ResolveExpr-Hom@L23426`
- `ResolveExpr-Alloc-Implicit@L23444`, `ResolveExpr-Alloc-Explicit@L23462`, `def.ResolveStmtSeqJudgementSet@L23480`, `ResolveStmtSeq-Empty@L23494`, `ResolveStmtSeq-Cons@L23511`, `ResolveExpr-Block@L23529`, `Validate-ModulePath-Ok@L23548`, `Validate-ModulePath-Reserved-Err@L23566`
- `req.ResolveItemFeatureOwnership@L23584`, `ResolveModule-Ok@L23598`, `ResolveItems-Empty@L23616`, `ResolveItems-Cons@L23631`, `def.ResolutionStateDomain@L23649`, `Res-Start@L23663`, `Res-Names@L23680`, `Res-Items@L23698`
- `ResolveModules-Ok@L23718`, `ResolveModules-Err-Parse@L23736`, `ResolveModules-Err-Resolve@L23754`

#### `checker.visibility`

Count: 15 total; 15 required; 0 recommended; 0 informative. Ledger line span: L19228-L19444.

- `def.DeclOfModuleItem@L19300`, `def.DeclOfExternProc@L19314`, `def.ModuleOfItem@L19328`, `def.ModuleOfExternProc@L19342`, `def.ExternBlockOfProc@L19356`, `def.ExternProcName@L19370`, `def.VisibilityOfDeclaration@L19384`, `def.SameAssembly@L19398`
- `Access-Public@L19412`, `Access-Internal@L19430`, `Access-Private@L19448`, `Access-Internal-Err@L19466`, `Access-Err@L19484`, `def.TopLevelDeclarationPredicate@L19502`, `TopLevelVis-Ok@L19516`

#### `checker.modules`

Count: 209 total; 209 required; 0 recommended; 0 informative. Ledger line span: L29191-L34273.

- `Parse-Import@L29550`, `def.ImportDeclAst@L29568`, `req.ImportDeclarationBindingSemanticsScope@L29586`, `Import-Path@L29602`, `Import-Path-Err@L29620`, `Bind-Import@L29638`, `Bind-Import-Err@L29656`, `ResolveItem-Import@L29674`
- `diagnostics.ImportDeclarations@L29723`, `req.ImportDeclarationDiagnosticOwnership@L29741`, `Parse-Using-Wildcard@L29785`, `Parse-Using-List@L29803`, `Parse-Using-Item@L29821`, `Parse-UsingSpec@L29839`, `Parse-UsingList-Empty@L29857`, `Parse-UsingList-Cons@L29875`
- `Parse-UsingListTail-End@L29893`, `Parse-UsingListTail-TrailingComma@L29911`, `Parse-UsingListTail-Comma@L29929`, `def.UsingDeclAst@L29947`, `req.UsingDeclarationBindingSemanticsScope@L29973`, `def.UsingSpecName@L29989`, `def.UsingSpecNames@L30005`, `Using-Item@L30019`
- `Using-Item-Public-Err@L30037`, `Using-List@L30055`, `Using-Wildcard-Warn@L30073`, `Using-Wildcard@L30091`, `Using-List-Dup@L30109`, `Using-List-Public-Err@L30127`, `Bind-Using@L30145`, `Bind-Using-Err@L30163`
- `ResolveItem-Using@L30181`, `diagnostics.UsingDeclarations@L30230`, `req.UsingDeclarationDiagnosticOwnership@L30251`, `def.StaticDeclTopLevelItems@L30287`, `Parse-Static-Decl@L30317`, `def.StaticDeclAst@L30335`, `req.StaticDeclModuleScopeBindingSemantics@L30353`, `def.StaticVisOk@L30369`
- `Bind-Static@L30383`, `WF-StaticDecl@L30401`, `WF-StaticDecl-Ann-Mismatch@L30419`, `WF-StaticDecl-MissingType@L30437`, `StaticVisOk-Err@L30455`, `ResolveItem-Static@L30473`, `def.StaticBindTypes@L30553`, `def.StaticBindList@L30567`
- `Emit-Static-Const@L30611`, `Emit-Static-Init@L30629`, `Emit-Static-Multi@L30647`, `InitFn@L30679`, `DeinitFn@L30711`, `def.StaticItems@L30729`, `def.StaticItemOf@L30743`, `def.StaticType@L30813`
- `def.StaticBindInfo@L30827`, `Lower-StaticInit-Item@L30871`, `Lower-StaticInitItems-Empty@L30889`, `Lower-StaticInitItems-Cons@L30906`, `Lower-StaticInit@L30924`, `InitCallIR@L30942`, `Lower-StaticDeinitNames-Empty@L30975`, `Lower-StaticDeinitNames-Cons-Resp@L30992`
- `Lower-StaticDeinitNames-Cons-NoResp@L31010`, `Lower-StaticDeinit-Item@L31028`, `Lower-StaticDeinitItems-Empty@L31046`, `Lower-StaticDeinitItems-Cons@L31063`, `Lower-StaticDeinit@L31081`, `diagnostics.StaticDeclarations@L31099`, `req.StaticDeclarationDiagnosticOwnership@L31119`, `Parse-ExternBlock@L31172`
- `Parse-ExternAbiOpt-None@L31190`, `Parse-ExternAbiOpt-String@L31208`, `Parse-ExternAbiOpt-Ident@L31226`, `Parse-ExternItemList-End@L31244`, `Parse-ExternItemList-Cons@L31262`, `def.ExternBlockAst@L31280`, `req.ExternBlockStaticSemanticsScope@L31301`, `Bind-ExternBlock@L31317`
- `WF-ExternBlock@L31335`, `ExternAbi-Unknown-Err@L31353`, `ResolveItem-ExternBlock@L31371`, `req.ModuleAggregationStaticSemanticsScope@L31601`, `def.NameCollectAfterParse@L31863`, `def.QualifiedLookupContext@L32452`, `def.ModuleAssemblyPathHelpers@L32469`, `def.ImportDeclarationsOfModule@L32485`
- `def.VisibleModulesAndNames@L32500`, `def.ModulePathPrefix@L32522`, `AliasExpand-None@L32538`, `AliasExpand-Yes@L32556`, `def.CurrentAsmPath@L32574`, `ModulePrefix-Direct@L32588`, `ModulePrefix-Current@L32606`, `ModulePrefix-None@L32624`
- `Resolve-ModulePath-Direct@L32642`, `Resolve-ModulePath-Current@L32660`, `ResolveModulePath-Err@L32678`, `def.ModuleByPath@L32696`, `def.ModuleOfPath@L32710`, `def.ItemNames@L32724`, `ItemOfPath@L32738`, `ItemOfPath-None@L32756`
- `def.ImportCoveragePredicates@L32774`, `def.ImportOkJudgementSet@L32789`, `Import-Ok-Local@L32803`, `Import-Ok-Covered@L32821`, `Import-Ok-Err@L32839`, `Resolve-Import-Direct@L32857`, `Resolve-Import-Current@L32875`, `Resolve-Import-Err@L32893`
- `Resolve-Using-Ok@L32911`, `Resolve-Using-Err@L32929`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32947`, `def.ModuleInitializationDependencyEnvironment@L32962`, `Reachable-Edge@L32978`, `Reachable-Step@L32996`, `def.ModuleInitializationPathHelpers@L33014`, `def.TypeRefsJudgementSet@L33030`
- `def.TypeReferenceEnvironmentAliases@L33044`, `TypeRef-Path@L33060`, `TypeRef-Using@L33078`, `TypeRef-Path-Local@L33096`, `TypeRef-Dynamic@L33114`, `TypeRef-ModalState@L33132`, `TypeRef-Apply@L33150`, `TypeRef-Perm@L33168`
- `TypeRef-Prim@L33186`, `TypeRef-Tuple@L33203`, `TypeRef-Array@L33221`, `TypeRef-Slice@L33239`, `TypeRef-Union@L33257`, `TypeRef-Func@L33275`, `TypeRef-String@L33293`, `TypeRef-Bytes@L33310`
- `TypeRef-Ptr@L33327`, `TypeRef-RawPtr@L33345`, `TypeRef-Range@L33363`, `TypeRef-RangeInclusive@L33381`, `TypeRef-RangeFrom@L33399`, `TypeRef-RangeTo@L33417`, `TypeRef-RangeToInclusive@L33435`, `TypeRef-RangeFull@L33453`
- `TypeRef-Ref-Path@L33470`, `TypeRef-Ref-Apply@L33488`, `TypeRef-Ref-ModalState@L33506`, `TypeRef-RecordExpr@L33524`, `TypeRef-EnumLiteral@L33542`, `TypeRef-QualBrace@L33560`, `TypeRef-Cast@L33578`, `TypeRef-Transmute@L33596`
- `TypeRef-CallTypeArgs@L33614`, `def.TypeRefsExprRules@L33632`, `def.NoSpecificTypeRefsExpr@L33646`, `TypeRef-Expr-Sub@L33660`, `TypeRef-RecordPattern@L33678`, `TypeRef-EnumPattern@L33696`, `TypeRef-LiteralPattern@L33714`, `TypeRef-WildcardPattern@L33731`
- `TypeRef-IdentifierPattern@L33748`, `TypeRef-TuplePattern@L33765`, `TypeRef-ModalPattern-None@L33783`, `TypeRef-ModalPattern-Record@L33800`, `TypeRef-RangePattern@L33818`, `TypeRef-Field-Explicit@L33836`, `TypeRef-Field-Implicit@L33854`, `TypeRefsExprs-Empty@L33871`
- `TypeRefsExprs-Cons@L33888`, `def.TypeRefsArgsJudgementSet@L33906`, `TypeRefsArgs-Empty@L33920`, `TypeRefsArgs-Cons@L33937`, `TypeRefsEnumPayload-None@L33955`, `TypeRefsEnumPayload-Tuple@L33972`, `TypeRefsEnumPayload-Record@L33990`, `TypeRefsFields-Empty@L34008`
- `TypeRefsFields-Cons@L34025`, `TypeRefsPayload-None@L34043`, `TypeRefsPayload-Tuple@L34060`, `TypeRefsPayload-Record@L34078`, `def.ValueReferenceEnvironmentAliases@L34096`, `def.ValueRefsJudgementSet@L34110`, `ValueRef-Ident@L34124`, `ValueRef-Ident-Local@L34142`
- `ValueRef-Qual@L34160`, `ValueRef-Qual-Local@L34178`, `ValueRef-QualApply@L34196`, `ValueRef-QualApply-Local@L34214`, `ValueRef-QualApply-Brace@L34232`, `def.ValueRefsRules@L34250`, `def.NoSpecificValueRefsExpr@L34264`, `ValueRef-Expr-Sub@L34278`
- `ValueRefsArgs-Empty@L34296`, `ValueRefsArgs-Cons@L34313`, `ValueRefsFields-Empty@L34331`, `ValueRefsFields-Cons@L34348`, `def.AstTraversalNodeHelpers@L34366`, `def.EnumVariantTypeSets@L34392`, `def.GeneralTypePositionSetHelpers@L34409`, `def.RecordMemberTypeSets@L34428`
- `def.ClassItemTypeSets@L34446`, `def.DeclarationTypePositions@L34464`, `def.TypePositionExpressions@L34485`, `def.TypeDeps@L34501`, `def.ValueDependencyExpressionSets@L34515`, `def.ValueDepsEagerLazy@L34533`, `def.ModuleDependencyGraphs@L34548`, `WF-Acyclic-Eager@L34568`
- `diagnostics.ModuleAggregation@L34632`
- `Parse-Import@L29550`, `def.ImportDeclAst@L29568`, `req.ImportDeclarationBindingSemanticsScope@L29586`, `Import-Path@L29602`, `Import-Path-Err@L29620`, `Bind-Import@L29638`, `Bind-Import-Err@L29656`, `ResolveItem-Import@L29674`
- `diagnostics.ImportDeclarations@L29723`, `req.ImportDeclarationDiagnosticOwnership@L29741`, `Parse-Using-Wildcard@L29785`, `Parse-Using-List@L29803`, `Parse-Using-Item@L29821`, `Parse-UsingSpec@L29839`, `Parse-UsingList-Empty@L29857`, `Parse-UsingList-Cons@L29875`
- `Parse-UsingListTail-End@L29893`, `Parse-UsingListTail-TrailingComma@L29911`, `Parse-UsingListTail-Comma@L29929`, `def.UsingDeclAst@L29947`, `req.UsingDeclarationBindingSemanticsScope@L29973`, `def.UsingSpecName@L29989`, `def.UsingSpecNames@L30005`, `Using-Item@L30019`
- `Using-Item-Public-Err@L30037`, `Using-List@L30055`, `Using-Wildcard-Warn@L30073`, `Using-Wildcard@L30091`, `Using-List-Dup@L30109`, `Using-List-Public-Err@L30127`, `Bind-Using@L30145`, `Bind-Using-Err@L30163`
- `ResolveItem-Using@L30181`, `diagnostics.UsingDeclarations@L30230`, `req.UsingDeclarationDiagnosticOwnership@L30251`, `def.StaticDeclTopLevelItems@L30287`, `Parse-Static-Decl@L30317`, `def.StaticDeclAst@L30335`, `req.StaticDeclModuleScopeBindingSemantics@L30353`, `def.StaticVisOk@L30369`
- `Bind-Static@L30383`, `WF-StaticDecl@L30401`, `WF-StaticDecl-Ann-Mismatch@L30419`, `WF-StaticDecl-MissingType@L30437`, `StaticVisOk-Err@L30455`, `ResolveItem-Static@L30473`, `def.StaticBindTypes@L30553`, `def.StaticBindList@L30567`
- `Emit-Static-Const@L30611`, `Emit-Static-Init@L30629`, `Emit-Static-Multi@L30647`, `InitFn@L30679`, `DeinitFn@L30711`, `def.StaticItems@L30729`, `def.StaticItemOf@L30743`, `def.StaticType@L30813`
- `def.StaticBindInfo@L30827`, `Lower-StaticInit-Item@L30871`, `Lower-StaticInitItems-Empty@L30889`, `Lower-StaticInitItems-Cons@L30906`, `Lower-StaticInit@L30924`, `InitCallIR@L30942`, `Lower-StaticDeinitNames-Empty@L30975`, `Lower-StaticDeinitNames-Cons-Resp@L30992`
- `Lower-StaticDeinitNames-Cons-NoResp@L31010`, `Lower-StaticDeinit-Item@L31028`, `Lower-StaticDeinitItems-Empty@L31046`, `Lower-StaticDeinitItems-Cons@L31063`, `Lower-StaticDeinit@L31081`, `diagnostics.StaticDeclarations@L31099`, `req.StaticDeclarationDiagnosticOwnership@L31119`, `Parse-ExternBlock@L31172`
- `Parse-ExternAbiOpt-None@L31190`, `Parse-ExternAbiOpt-String@L31208`, `Parse-ExternAbiOpt-Ident@L31226`, `Parse-ExternItemList-End@L31244`, `Parse-ExternItemList-Cons@L31262`, `def.ExternBlockAst@L31280`, `req.ExternBlockStaticSemanticsScope@L31301`, `Bind-ExternBlock@L31317`
- `WF-ExternBlock@L31335`, `ExternAbi-Unknown-Err@L31353`, `ResolveItem-ExternBlock@L31371`, `req.ModuleAggregationStaticSemanticsScope@L31601`, `def.NameCollectAfterParse@L31863`, `def.QualifiedLookupContext@L32452`, `def.ModuleAssemblyPathHelpers@L32469`, `def.ImportDeclarationsOfModule@L32485`
- `def.VisibleModulesAndNames@L32500`, `def.ModulePathPrefix@L32522`, `AliasExpand-None@L32538`, `AliasExpand-Yes@L32556`, `def.CurrentAsmPath@L32574`, `ModulePrefix-Direct@L32588`, `ModulePrefix-Current@L32606`, `ModulePrefix-None@L32624`
- `Resolve-ModulePath-Direct@L32642`, `Resolve-ModulePath-Current@L32660`, `ResolveModulePath-Err@L32678`, `def.ModuleByPath@L32696`, `def.ModuleOfPath@L32710`, `def.ItemNames@L32724`, `ItemOfPath@L32738`, `ItemOfPath-None@L32756`
- `def.ImportCoveragePredicates@L32774`, `def.ImportOkJudgementSet@L32789`, `Import-Ok-Local@L32803`, `Import-Ok-Covered@L32821`, `Import-Ok-Err@L32839`, `Resolve-Import-Direct@L32857`, `Resolve-Import-Current@L32875`, `Resolve-Import-Err@L32893`
- `Resolve-Using-Ok@L32911`, `Resolve-Using-Err@L32929`, `req.ResolvedItemAccessibilityOwnedByVisibilityChapter@L32947`, `def.ModuleInitializationDependencyEnvironment@L32962`, `Reachable-Edge@L32978`, `Reachable-Step@L32996`, `def.ModuleInitializationPathHelpers@L33014`, `def.TypeRefsJudgementSet@L33030`
- `def.TypeReferenceEnvironmentAliases@L33044`, `TypeRef-Path@L33060`, `TypeRef-Using@L33078`, `TypeRef-Path-Local@L33096`, `TypeRef-Dynamic@L33114`, `TypeRef-ModalState@L33132`, `TypeRef-Apply@L33150`, `TypeRef-Perm@L33168`
- `TypeRef-Prim@L33186`, `TypeRef-Tuple@L33203`, `TypeRef-Array@L33221`, `TypeRef-Slice@L33239`, `TypeRef-Union@L33257`, `TypeRef-Func@L33275`, `TypeRef-String@L33293`, `TypeRef-Bytes@L33310`
- `TypeRef-Ptr@L33327`, `TypeRef-RawPtr@L33345`, `TypeRef-Range@L33363`, `TypeRef-RangeInclusive@L33381`, `TypeRef-RangeFrom@L33399`, `TypeRef-RangeTo@L33417`, `TypeRef-RangeToInclusive@L33435`, `TypeRef-RangeFull@L33453`
- `TypeRef-Ref-Path@L33470`, `TypeRef-Ref-Apply@L33488`, `TypeRef-Ref-ModalState@L33506`, `TypeRef-RecordExpr@L33524`, `TypeRef-EnumLiteral@L33542`, `TypeRef-QualBrace@L33560`, `TypeRef-Cast@L33578`, `TypeRef-Transmute@L33596`
- `TypeRef-CallTypeArgs@L33614`, `def.TypeRefsExprRules@L33632`, `def.NoSpecificTypeRefsExpr@L33646`, `TypeRef-Expr-Sub@L33660`, `TypeRef-RecordPattern@L33678`, `TypeRef-EnumPattern@L33696`, `TypeRef-LiteralPattern@L33714`, `TypeRef-WildcardPattern@L33731`
- `TypeRef-IdentifierPattern@L33748`, `TypeRef-TuplePattern@L33765`, `TypeRef-ModalPattern-None@L33783`, `TypeRef-ModalPattern-Record@L33800`, `TypeRef-RangePattern@L33818`, `TypeRef-Field-Explicit@L33836`, `TypeRef-Field-Implicit@L33854`, `TypeRefsExprs-Empty@L33871`
- `TypeRefsExprs-Cons@L33888`, `def.TypeRefsArgsJudgementSet@L33906`, `TypeRefsArgs-Empty@L33920`, `TypeRefsArgs-Cons@L33937`, `TypeRefsEnumPayload-None@L33955`, `TypeRefsEnumPayload-Tuple@L33972`, `TypeRefsEnumPayload-Record@L33990`, `TypeRefsFields-Empty@L34008`
- `TypeRefsFields-Cons@L34025`, `TypeRefsPayload-None@L34043`, `TypeRefsPayload-Tuple@L34060`, `TypeRefsPayload-Record@L34078`, `def.ValueReferenceEnvironmentAliases@L34096`, `def.ValueRefsJudgementSet@L34110`, `ValueRef-Ident@L34124`, `ValueRef-Ident-Local@L34142`
- `ValueRef-Qual@L34160`, `ValueRef-Qual-Local@L34178`, `ValueRef-QualApply@L34196`, `ValueRef-QualApply-Local@L34214`, `ValueRef-QualApply-Brace@L34232`, `def.ValueRefsRules@L34250`, `def.NoSpecificValueRefsExpr@L34264`, `ValueRef-Expr-Sub@L34278`
- `ValueRefsArgs-Empty@L34296`, `ValueRefsArgs-Cons@L34313`, `ValueRefsFields-Empty@L34331`, `ValueRefsFields-Cons@L34348`, `def.AstTraversalNodeHelpers@L34366`, `def.EnumVariantTypeSets@L34392`, `def.GeneralTypePositionSetHelpers@L34409`, `def.RecordMemberTypeSets@L34428`
- `def.ClassItemTypeSets@L34446`, `def.DeclarationTypePositions@L34464`, `def.TypePositionExpressions@L34485`, `def.TypeDeps@L34501`, `def.ValueDependencyExpressionSets@L34515`, `def.ValueDepsEagerLazy@L34533`, `def.ModuleDependencyGraphs@L34548`, `WF-Acyclic-Eager@L34568`
- `diagnostics.ModuleAggregation@L34632`

### Types, Permissions, Declarations, And Static Semantics

#### `checker.binding-state`

Count: 72 total; 72 required; 0 recommended; 0 informative. Ledger line span: L14425-L15866.

- `def.BindingStateDomain@L14497`, `def.BindInfo@L14511`, `def.BindingEnvironment@L14528`, `def.BindingScopeStackOps@L14543`, `def.BindingLookup@L14558`, `def.BindingUpdate@L14575`, `def.BindingIntro@L14592`, `def.BindingStateJoin@L14701`
- `def.BindingStateTransitionSet@L14720`, `Trans-Move-Whole@L14734`, `Trans-Move-Field@L14752`, `Trans-Move-Field-Partial@L14770`, `Trans-Partial-To-Moved@L14788`, `Trans-Reassign@L14806`, `Trans-Moved-NoAccess@L14824`, `Trans-Partial-NoAccess@L14842`
- `Trans-Let-NoReassign@L14860`, `def.BindingInfoJoin@L14878`, `def.BindingScopeJoin@L14894`, `def.BindingEnvironmentJoin@L14910`, `def.FieldHead@L14993`, `def.FieldPathOf@L15013`, `def.PlacePath@L15032`, `def.ArgumentPassExpression@L15112`
- `def.AccessStateOk@L15130`, `def.PartialMoveStateUpdate@L15146`, `def.ExpressionTypeLookupForAccess@L15162`, `def.AccessOk@L15177`, `def.BindingMovabilityOperator@L15193`, `def.MoveExpressionPredicate@L15208`, `def.InitializationResponsibility@L15223`, `def.BindingInitializerExpression@L15240`
- `def.BindingInitializerScope@L15256`, `def.TemporaryScope@L15272`, `def.TemporaryValuePredicate@L15288`, `def.TemporaryEvaluationOrder@L15302`, `def.ControlStatementExpression@L15323`, `def.TemporaryDropOrder@L15339`, `def.OptionalExpressionList@L15354`, `def.StatementExpressions@L15369`
- `def.StatementAndBindingScopes@L15396`, `def.BlockStatements@L15413`, `def.StatementBlocks@L15427`, `def.StatementSubExpressions@L15445`, `def.StatementSubStatements@L15461`, `def.SubBlocks@L15479`, `def.MapEntries@L15495`, `def.MapUnion@L15509`
- `def.IntroduceAllBindings@L15523`, `def.BindInfoMap@L15537`, `def.EffectiveMovability@L15551`, `def.BindingNames@L15586`, `def.JoinAllBindings@L15600`, `def.ConsumeOnMove@L15662`, `def.MoveExpressionInnerPlace@L15678`, `def.BindingJudgmentSet@L15692`
- `def.StaticBindingMaps@L15706`, `def.ProcedureEntryBindingScopes@L15724`, `def.ParameterBindingMap@L15739`, `def.MethodParameterBindingMap@L15754`, `def.ParameterTypeMap@L15768`, `def.ParameterMoveAndResponsibility@L15783`, `def.InitialBindingEnvironment@L15798`, `def.BindCheck@L15826`
- `def.ProcedureBindingCheck@L15840`, `def.MethodParametersForBinding@L15854`, `def.MethodBindingCheck@L15868`, `def.ClassMethodBindingCheck@L15882`, `def.StateMethodBindingCheck@L15896`, `def.TransitionBindingCheck@L15910`, `def.BindingDiagnosticReferences@L15924`, `req.FeatureSpecificBJudgmentOwnership@L15938`

#### `checker.permission-state`

Count: 18 total; 18 required; 0 recommended; 0 informative. Ledger line span: L14536-L15740.

- `def.PermissionOfType@L14608`, `def.PermissionActivityDomain@L14623`, `def.PermissionEnvironment@L14637`, `def.PermissionScopeStackOps@L14653`, `def.PermissionLookup@L14668`, `def.PermissionUpdate@L14685`, `def.PermissionStateJoin@L14928`, `def.PermissionAtScope@L14943`
- `def.PermissionScopeJoin@L14959`, `def.PermissionEnvironmentJoin@L14973`, `def.AccessPathPrefixes@L15048`, `def.AccessPathOk@L15064`, `def.SuspendUniquePath@L15078`, `def.ReactivatePermissionKeys@L15097`, `def.JoinAllPermissions@L15616`, `def.PermissionTopScopeOps@L15632`
- `def.PermissionRoots@L15648`, `def.InitialPermissionEnvironment@L15812`

#### `checker.regions`

Count: 12 total; 12 required; 0 recommended; 0 informative. Ledger line span: L15494-L16037.

- `def.RegionBindingInfo@L15566`, `def.RegionOptionsFields@L15956`, `def.RegionOptionsDecl@L15973`, `def.RegionPreallocation@L15988`, `def.RegionActiveType@L16003`, `def.FreshRegion@L16017`, `def.RegionOptionsExpression@L16031`, `def.RegionBind@L16046`
- `def.InnermostActiveRegion@L16062`, `def.FrameBind@L16079`, `req.RegionSyntheticIdentifierRestriction@L16095`, `req.FrameSyntheticIdentifierRestriction@L16109`

#### `checker.provenance`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L16053-L16840.

- `def.ProvenanceTags@L16125`, `def.RegionNesting@L16139`, `def.StrictProvenanceLifetimeOrder@L16153`, `def.ProvenanceLifetimeOrder@L16167`, `def.FrameTarget@L16181`, `def.FrameTargetProvenanceOrder@L16196`, `def.ProvenanceJoin@L16211`, `def.JoinAllProvenance@L16228`
- `def.ProvenanceEnvironmentShape@L16246`, `def.ProvenanceScopeAccessors@L16264`, `def.ProvenanceScopeStackOps@L16281`, `def.ProvenanceLookup@L16296`, `def.ProvenanceIntro@L16312`, `def.ProvenanceIntroAll@L16326`, `def.ParameterProvenanceInitialization@L16341`, `def.ProvenanceRegionEntryResolution@L16356`
- `def.ProvenanceRegionAliasIntro@L16375`, `def.FreshRegionTag@L16389`, `def.AllocationRegionTagSelection@L16403`, `def.FreshRegionExpression@L16421`, `def.ProvenanceJudgmentSets@L16435`, `def.CaseBodyProvenance@L16452`, `def.CasePatternProvenanceEnvironment@L16467`, `def.CaseProvenance@L16481`
- `def.CaseElseProvenance@L16495`, `rules.ProvenanceChecking@L16510`, `P-If-Is@L16528`, `P-If-Cases@L16546`, `def.ClosureCaptureProvenance@L16566`, `def.ClosureTargetProvenance@L16580`, `def.ClosureLocalSharedCaptures@L16596`, `def.ClosureEscapeCheck@L16610`
- `P-Closure-NonCapturing@L16626`, `P-Closure-Capturing@L16644`, `P-Closure-Escape-Err@L16663`, `def.FrameProvenance@L16682`, `def.BreakProvenance@L16700`, `def.IteratorElementProvenance@L16714`, `def.InfiniteLoopProvenance@L16728`, `def.FiniteLoopProvenance@L16743`
- `def.ExtendProvenanceForPattern@L16758`, `P-Loop-Infinite@L16772`, `P-Loop-Conditional@L16790`, `P-Loop-Iter@L16808`, `def.ProvenanceEscapeHelpers@L16826`, `req.NoGeneralHeapEscapeConversion@L16840`, `def.BindingProvenance@L16854`, `def.StaticBindingProvenance@L16870`
- `def.AssignmentProvenanceEscapeCheck@L16884`, `def.ProvenanceEscapeJudgmentSet@L16898`, `req.ProvenanceEscapeCheckPurpose@L16912`

#### `checker.types`

Count: 283 total; 181 required; 0 recommended; 0 informative. Ledger line span: L23734-L40932.

- `def.TypeEquivalenceJudgementSet@L23806`, `def.ConstLenJudgementSet@L23820`, `ConstLen-Lit@L23834`, `ConstLen-Path@L23852`, `ConstLen-Err@L23870`, `def.UnionMembersEquivalence@L23888`, `T-Equiv-Prim@L23902`, `T-Equiv-Perm@L23920`
- `T-Equiv-Tuple@L23938`, `T-Equiv-Array@L23956`, `T-Equiv-Slice@L23974`, `T-Equiv-Func@L23992`, `T-Equiv-Closure@L24010`, `T-Equiv-Union@L24028`, `T-Equiv-Path@L24046`, `T-Equiv-ModalState@L24064`
- `T-Equiv-String@L24082`, `T-Equiv-Bytes@L24100`, `T-Equiv-Range@L24118`, `T-Equiv-RangeInclusive@L24136`, `T-Equiv-RangeFrom@L24154`, `T-Equiv-RangeTo@L24172`, `T-Equiv-RangeToInclusive@L24190`, `T-Equiv-RangeFull@L24208`
- `T-Equiv-Ptr@L24226`, `T-Equiv-RawPtr@L24244`, `T-Equiv-Dynamic@L24262`, `T-Equiv-Apply@L24280`, `T-Equiv-Opaque@L24298`, `T-Equiv-Refine@L24316`, `def.PredicateEquivalence@L24334`, `T-Equiv-Refine-Norm@L24348`
- `T-Equiv-Refl@L24366`, `T-Equiv-Sym@L24384`, `T-Equiv-Trans@L24402`, `def.SubtypingJudgementSet@L24423`, `req.NoIntegerNumericSubtyping@L24437`, `req.NoFloatNumericSubtyping@L24451`, `req.PermissionAdmissibilityOwnedByChapter10@L24465`, `Sub-Perm@L24479`
- `Sub-Never@L24497`, `Sub-Tuple@L24515`, `Sub-Array@L24533`, `Sub-Slice@L24551`, `Sub-Range@L24569`, `Sub-RangeInclusive@L24587`, `Sub-RangeFrom@L24605`, `Sub-RangeTo@L24623`
- `Sub-RangeToInclusive@L24641`, `Sub-RangeFull@L24659`, `Sub-Ptr-State@L24677`, `Sub-Modal-Niche@L24695`, `Sub-Func@L24713`, `Sub-Closure@L24731`, `Sub-Async@L24749`, `def.UnionMember@L40436`
- `Sub-Member-Union@L40450`, `Sub-Union-Width@L40468`, `def.VarianceDomain@L24818`, `def.VarianceOfSignature@L24832`, `def.VarianceOf@L24846`, `def.VarianceSatisfied@L24860`, `Sub-Generic@L24878`, `Sub-Generic-Invariant-Err@L24896`
- `Sub-RangeToInclusive@L24641`, `Sub-RangeFull@L24659`, `Sub-Ptr-State@L24677`, `Sub-Modal-Niche@L24695`, `Sub-Func@L24713`, `Sub-Closure@L24731`, `Sub-Async@L24749`, `def.UnionMember@L40436`
- `Sub-Member-Union@L40450`, `Sub-Union-Width@L40468`, `def.VarianceDomain@L24818`, `def.VarianceOfSignature@L24832`, `def.VarianceOf@L24846`, `def.VarianceSatisfied@L24860`, `Sub-Generic@L24878`, `Sub-Generic-Invariant-Err@L24896`
- `Sub-Generic-Covariant-Err@L24914`, `Sub-Generic-Contravariant-Err@L24932`, `Sub-Refl@L24950`, `Sub-Trans@L24968`, `def.TypeInferenceJudgementSet@L24989`, `def.TypeEqualityConstraint@L25003`, `def.TypeEqualityConstraintSet@L25017`, `req.ConstraintGenerationFeatureLocal@L25031`
- `def.TypeVariableDomain@L25045`, `def.TypeVariablesOfType@L25059`, `def.SubstitutionDomain@L25073`, `def.SubstitutionDefinedDomain@L25087`, `def.IdentitySubstitution@L25101`, `def.SubstitutionApplication@L25115`, `def.SubstitutionComposition@L25139`, `def.UnificationStateDomain@L25153`
- `Unify-Empty@L25167`, `Unify-Eq@L25184`, `Unify-Var-L@L25202`, `Unify-Var-R@L25220`, `Unify-Occurs-Fail@L25238`, `Unify-Tuple@L25256`, `Unify-Tuple-Fail@L25274`, `Unify-Array@L25292`
- `Unify-Array-Len-Fail@L25310`, `Unify-Slice@L25328`, `Unify-Perm@L25346`, `Unify-Perm-Fail@L25364`, `Unify-Func@L25382`, `Unify-Func-Fail@L25401`, `Unify-Closure@L25419`, `Unify-Closure-Fail@L25437`
- `Unify-Ptr@L25455`, `Unify-Ptr-State-Fail@L25473`, `Unify-RawPtr@L25491`, `Unify-RawPtr-Qual-Fail@L25509`, `Unify-Apply@L25527`, `Unify-Apply-Fail@L25545`, `Unify-Range@L25563`, `Unify-RangeInclusive@L25581`
- `Unify-RangeFrom@L25599`, `Unify-RangeTo@L25617`, `Unify-RangeToInclusive@L25635`, `Unify-Refine@L25653`, `Unify-Refine-Pred-Fail@L25671`, `Unify-Prim-Fail@L25689`, `Unify-Rigid-Fail@L25707`, `Unify-Ctor-Mismatch@L25732`
- `Unify-Ok@L25750`, `Unify-Err@L25768`, `Solve-Unify@L25786`, `Solve-Fail@L25804`, `Syn-Expr@L25822`, `Syn-Ident@L25840`, `Syn-Unit@L25858`, `Syn-Tuple@L25875`
- `Syn-Call@L25893`, `Syn-Call-Err@L25911`, `Chk-Subsumption-Modal-NonNiche@L25929`, `Chk-Subsumption@L25947`, `Chk-Null-Ptr@L25965`, `def.PtrNullExpectedType@L25983`, `Syn-PtrNull-Err@L25997`, `Chk-PtrNull-Err@L26015`
- `req.FeatureLocalSynthesisAndCheckingOwnership@L26033`, `property.TypeSystemMetatheory.Intro@L26050`, `Progress@L26064`, `Preservation@L26083`, `No-Use-After-Free@L26099`, `No-Double-Free@L26115`, `No-Dangling-Pointers@L26131`, `Exclusivity-Invariant@L26147`
- `Permission-Preservation@L26163`, `State-Determinism@L26179`, `No-Resurrection@L26195`, `Data-Race-Freedom@L26211`, `Fork-Join-Guarantee@L26227`, `Key-Serialization@L26243`, `Async-Key-Safety@L26259`, `req.PermissionQualifiedSubtypingPermissionEquality@L29428`
- `Parse-Record@L38235`, `Parse-RecordBody@L38253`, `Parse-RecordMemberList-End@L38271`, `Parse-RecordMemberList-Cons@L38289`, `Parse-RecordMember-Method@L38307`, `Parse-RecordMember-AssociatedType@L38325`, `Parse-RecordMember-Field@L38343`, `Parse-RecordFieldDeclAfterVis@L38361`
- `Parse-RecordFieldInitOpt-None@L38379`, `Parse-RecordFieldInitOpt-Yes@L38397`, `Parse-Record-Literal@L38415`, `def.RecordDeclAst@L38433`, `def.RecordMemberAst@L38450`, `def.RecordExprAst@L38468`, `def.RecordMembersSelectors@L38482`, `def.RecordPath@L38497`
- `Bind-Record@L38515`, `Resolve-RecordPath@L38532`, `ResolveQual-Name-Record@L38550`, `ResolveQual-Apply-RecordLit@L38568`, `ResolveItem-Record@L38586`, `def.RecordFieldInitOk@L38604`, `def.RecordFieldVisibility@L38618`, `WF-Record@L38633`
- `WF-Record-DupField@L38651`, `WF-RecordDecl@L38669`, `FieldVisOk-Err@L38687`, `def.RecordDefaultConstructible@L38705`, `def.RecordCallee@L38719`, `T-Record-Default@L38733`, `def.RecordFieldNameSets@L38751`, `def.RecordFieldLookup@L38769`
- `T-Record-Literal@L38785`, `EvalSigma-Record@L38819`, `EvalSigma-Record-Ctrl@L38837`, `ApplyRecordCtorSigma@L38869`, `ApplyRecordCtorSigma-Ctrl@L38887`, `Layout-Record-Empty@L38928`, `Layout-Record-Cons@L38945`, `Size-Record@L38963`
- `Align-Record@L38981`, `Layout-Record@L38999`, `LowerFieldInits-Empty@L39087`, `LowerFieldInits-Cons@L39104`, `Lower-Expr-Record@L39122`, `Lower-CallIR-RecordCtor@L39140`, `diagnostics.Records@L39158`, `Parse-Enum@L39225`
- `Parse-EnumBody@L39243`, `Parse-VariantMembers-Empty@L39261`, `Parse-VariantMembers-Cons@L39279`, `Parse-VariantSep-End@L39297`, `Parse-VariantSep-Terminator@L39315`, `Parse-Variant@L39333`, `Parse-VariantPayloadOpt-None@L39351`, `Parse-VariantPayloadOpt-Tuple@L39369`
- `Parse-VariantPayloadOpt-Record@L39387`, `Parse-VariantDiscriminantOpt-None@L39405`, `Parse-VariantDiscriminantOpt-Yes@L39423`, `req.EnumLiteralResolutionOwnership@L39455`, `def.EnumDeclAst@L39469`, `def.VariantDeclAst@L39486`, `def.EnumVariantHelpers@L39502`, `Bind-Enum@L39524`
- `def.EnumPayloadWellFormedness@L39541`, `Resolve-EnumUnit@L39559`, `Resolve-EnumTuple@L39577`, `Resolve-EnumRecord@L39595`, `ResolveQual-Name-Enum@L39613`, `ResolveQual-Apply-Enum-Tuple@L39631`, `ResolveQual-Apply-Enum-Record@L39649`, `ResolveItem-Enum@L39667`
- `def.EnumDiscriminantSequence@L39685`, `Enum-Disc-NotInt@L39707`, `Enum-Disc-Invalid@L39725`, `Enum-Disc-Negative@L39743`, `Enum-Disc-Dup@L39761`, `Enum-Empty-Err@L39779`, `Enum-Variant-Dup@L39797`, `def.EnumDiscriminantType@L39815`
- `WF-EnumDecl@L39834`, `def.EnumLiteralPayloadHelpers@L39852`, `T-Enum-Lit-Unit@L39870`, `Enum-Lit-Unknown@L39888`, `T-Enum-Lit-Tuple@L39906`, `Enum-Lit-Tuple-Arity-Err@L39924`, `T-Enum-Lit-Record@L39942`, `Enum-Lit-Record-MissingField@L39960`
- `EvalSigma-Enum-Unit@L39994`, `EvalSigma-Enum-Tuple@L40011`, `EvalSigma-Enum-Tuple-Ctrl@L40029`, `EvalSigma-Enum-Record@L40047`, `EvalSigma-Enum-Record-Ctrl@L40065`, `Layout-Enum-Tagged@L40112`, `Size-Enum@L40130`, `Align-Enum@L40148`
- `Layout-Enum@L40166`, `Lower-Expr-Enum-Unit@L40214`, `Lower-Expr-Enum-Tuple@L40231`, `Lower-Expr-Enum-Record@L40249`, `diagnostics.Enums@L40267`, `req.UnionIntroductionSemantic@L40314`, `Parse-UnionTail-None@L40330`, `Parse-UnionTail-Cons@L40348`
- `def.TypeUnionAst@L40366`, `def.UnionMemberSets@L40382`, `WF-Union@L40400`, `WF-Union-TooFew@L40418`, `def.UnionMember@L40436`, `Sub-Member-Union@L40450`, `Sub-Union-Width@L40468`, `T-Union-Intro@L40486`
- `Union-DirectAccess-Err@L40504`, `req.UnionMatchingPropagationOwnership@L40522`, `Layout-Union-Niche@L40709`, `Layout-Union-Tagged@L40727`, `Size-Union@L40745`, `Align-Union@L40763`, `Layout-Union@L40781`, `req.UnionDiagnosticOwnership@L40899`
- `Parse-Type-Alias@L40938`, `def.TypeAliasDeclAst@L40956`, `def.TypeAliasAccessors@L40972`, `Bind-TypeAlias@L40990`, `ResolveItem-TypeAlias@L41007`, `def.AliasNormalization@L41025`, `def.AliasPathNormalization@L41061`, `def.AliasTransparent@L41078`
- `def.AliasGraph@L41092`, `def.TypePaths@L41106`, `def.TypePathsOfModalRef@L41142`, `def.AliasCycle@L41157`, `TypeAlias-Ok@L41171`, `TypeAlias-Recursive-Err@L41188`, `req.TypeAliasDynamicSemantics@L41205`, `Size-Alias@L41223`
- `Align-Alias@L41241`, `Layout-Alias@L41259`, `req.TypeAliasDiagnosticOwnership@L41291`
- `Permission-Preservation@L26163`, `State-Determinism@L26179`, `No-Resurrection@L26195`, `Data-Race-Freedom@L26211`, `Fork-Join-Guarantee@L26227`, `Key-Serialization@L26243`, `Async-Key-Safety@L26259`, `req.PermissionQualifiedSubtypingPermissionEquality@L29428`
- `Parse-Record@L38235`, `Parse-RecordBody@L38253`, `Parse-RecordMemberList-End@L38271`, `Parse-RecordMemberList-Cons@L38289`, `Parse-RecordMember-Method@L38307`, `Parse-RecordMember-AssociatedType@L38325`, `Parse-RecordMember-Field@L38343`, `Parse-RecordFieldDeclAfterVis@L38361`
- `Parse-RecordFieldInitOpt-None@L38379`, `Parse-RecordFieldInitOpt-Yes@L38397`, `Parse-Record-Literal@L38415`, `def.RecordDeclAst@L38433`, `def.RecordMemberAst@L38450`, `def.RecordExprAst@L38468`, `def.RecordMembersSelectors@L38482`, `def.RecordPath@L38497`
- `Bind-Record@L38515`, `Resolve-RecordPath@L38532`, `ResolveQual-Name-Record@L38550`, `ResolveQual-Apply-RecordLit@L38568`, `ResolveItem-Record@L38586`, `def.RecordFieldInitOk@L38604`, `def.RecordFieldVisibility@L38618`, `WF-Record@L38633`
- `WF-Record-DupField@L38651`, `WF-RecordDecl@L38669`, `FieldVisOk-Err@L38687`, `def.RecordDefaultConstructible@L38705`, `def.RecordCallee@L38719`, `T-Record-Default@L38733`, `def.RecordFieldNameSets@L38751`, `def.RecordFieldLookup@L38769`
- `T-Record-Literal@L38785`, `EvalSigma-Record@L38819`, `EvalSigma-Record-Ctrl@L38837`, `ApplyRecordCtorSigma@L38869`, `ApplyRecordCtorSigma-Ctrl@L38887`, `Layout-Record-Empty@L38928`, `Layout-Record-Cons@L38945`, `Size-Record@L38963`
- `Align-Record@L38981`, `Layout-Record@L38999`, `LowerFieldInits-Empty@L39087`, `LowerFieldInits-Cons@L39104`, `Lower-Expr-Record@L39122`, `Lower-CallIR-RecordCtor@L39140`, `diagnostics.Records@L39158`, `Parse-Enum@L39225`
- `Parse-EnumBody@L39243`, `Parse-VariantMembers-Empty@L39261`, `Parse-VariantMembers-Cons@L39279`, `Parse-VariantSep-End@L39297`, `Parse-VariantSep-Terminator@L39315`, `Parse-Variant@L39333`, `Parse-VariantPayloadOpt-None@L39351`, `Parse-VariantPayloadOpt-Tuple@L39369`
- `Parse-VariantPayloadOpt-Record@L39387`, `Parse-VariantDiscriminantOpt-None@L39405`, `Parse-VariantDiscriminantOpt-Yes@L39423`, `req.EnumLiteralResolutionOwnership@L39455`, `def.EnumDeclAst@L39469`, `def.VariantDeclAst@L39486`, `def.EnumVariantHelpers@L39502`, `Bind-Enum@L39524`
- `def.EnumPayloadWellFormedness@L39541`, `Resolve-EnumUnit@L39559`, `Resolve-EnumTuple@L39577`, `Resolve-EnumRecord@L39595`, `ResolveQual-Name-Enum@L39613`, `ResolveQual-Apply-Enum-Tuple@L39631`, `ResolveQual-Apply-Enum-Record@L39649`, `ResolveItem-Enum@L39667`
- `def.EnumDiscriminantSequence@L39685`, `Enum-Disc-NotInt@L39707`, `Enum-Disc-Invalid@L39725`, `Enum-Disc-Negative@L39743`, `Enum-Disc-Dup@L39761`, `Enum-Empty-Err@L39779`, `Enum-Variant-Dup@L39797`, `def.EnumDiscriminantType@L39815`
- `WF-EnumDecl@L39834`, `def.EnumLiteralPayloadHelpers@L39852`, `T-Enum-Lit-Unit@L39870`, `Enum-Lit-Unknown@L39888`, `T-Enum-Lit-Tuple@L39906`, `Enum-Lit-Tuple-Arity-Err@L39924`, `T-Enum-Lit-Record@L39942`, `Enum-Lit-Record-MissingField@L39960`
- `EvalSigma-Enum-Unit@L39994`, `EvalSigma-Enum-Tuple@L40011`, `EvalSigma-Enum-Tuple-Ctrl@L40029`, `EvalSigma-Enum-Record@L40047`, `EvalSigma-Enum-Record-Ctrl@L40065`, `Layout-Enum-Tagged@L40112`, `Size-Enum@L40130`, `Align-Enum@L40148`
- `Layout-Enum@L40166`, `Lower-Expr-Enum-Unit@L40214`, `Lower-Expr-Enum-Tuple@L40231`, `Lower-Expr-Enum-Record@L40249`, `diagnostics.Enums@L40267`, `req.UnionIntroductionSemantic@L40314`, `Parse-UnionTail-None@L40330`, `Parse-UnionTail-Cons@L40348`
- `def.TypeUnionAst@L40366`, `def.UnionMemberSets@L40382`, `WF-Union@L40400`, `WF-Union-TooFew@L40418`, `def.UnionMember@L40436`, `Sub-Member-Union@L40450`, `Sub-Union-Width@L40468`, `T-Union-Intro@L40486`
- `Union-DirectAccess-Err@L40504`, `req.UnionMatchingPropagationOwnership@L40522`, `Layout-Union-Niche@L40709`, `Layout-Union-Tagged@L40727`, `Size-Union@L40745`, `Align-Union@L40763`, `Layout-Union@L40781`, `req.UnionDiagnosticOwnership@L40899`
- `Parse-Type-Alias@L40938`, `def.TypeAliasDeclAst@L40956`, `def.TypeAliasAccessors@L40972`, `Bind-TypeAlias@L40990`, `ResolveItem-TypeAlias@L41007`, `def.AliasNormalization@L41025`, `def.AliasPathNormalization@L41061`, `def.AliasTransparent@L41078`
- `def.AliasGraph@L41092`, `def.TypePaths@L41106`, `def.TypePathsOfModalRef@L41142`, `def.AliasCycle@L41157`, `TypeAlias-Ok@L41171`, `TypeAlias-Recursive-Err@L41188`, `req.TypeAliasDynamicSemantics@L41205`, `Size-Alias@L41223`
- `Align-Alias@L41241`, `Layout-Alias@L41259`, `req.TypeAliasDiagnosticOwnership@L41291`

#### `checker.attributes`

Count: 16 total; 16 required; 0 recommended; 0 informative. Ledger line span: L26829-L27272.

- `req.MalformedAttributeSyntaxIllFormed@L26901`, `def.AttributeTargetDomain@L26915`, `def.AttributeRegistry@L26929`, `def.VendorAttributeRegistryInitial@L26943`, `def.SpecAttributeRegistry@L26957`, `def.SpecAttributeTargets@L26980`, `def.AttributeListWellFormedJudgementSet@L27016`, `AttrList-Ok@L27030`
- `AttrList-Unknown@L27048`, `AttrList-Target-Err@L27066`, `def.AttributeStaticSemantics.Helpers2@L27084`, `req.MemoryOrderAttributeTargets@L27098`, `req.AttributeListWellFormednessCheck@L27112`, `req.MultipleAttributeListConcatenation@L27126`, `req.FfiAttributeOwnership@L27140`, `conformance.VendorAttributeStaticSemantics@L27346`
- `req.MalformedAttributeSyntaxIllFormed@L26901`, `def.AttributeTargetDomain@L26915`, `def.AttributeRegistry@L26929`, `def.VendorAttributeRegistryInitial@L26943`, `def.SpecAttributeRegistry@L26957`, `def.SpecAttributeTargets@L26980`, `def.AttributeListWellFormedJudgementSet@L27016`, `AttrList-Ok@L27030`
- `AttrList-Unknown@L27048`, `AttrList-Target-Err@L27066`, `def.AttributeStaticSemantics.Helpers2@L27084`, `req.MemoryOrderAttributeTargets@L27098`, `req.AttributeListWellFormednessCheck@L27112`, `req.MultipleAttributeListConcatenation@L27126`, `req.FfiAttributeOwnership@L27140`, `conformance.VendorAttributeStaticSemantics@L27346`

#### `checker.attributes.layout`

Count: 10 total; 10 required; 0 recommended; 0 informative. Ledger line span: L27401-L27559.

- `req.LayoutCRecordSemantics@L27475`, `req.LayoutCEnumSemantics@L27492`, `req.LayoutExplicitEnumDiscriminant@L27509`, `req.LayoutPackedRecordSemantics@L27528`, `req.PackedFieldReferenceRequiresUnsafe@L27545`, `req.LayoutAlignSemantics@L27561`, `def.ValidLayoutAttributeCombinations@L27580`, `def.InvalidLayoutAttributeCombinations@L27600`
- `def.LayoutAttributeApplicability@L27615`, `req.LayoutAttributeConstraints@L27633`
- `req.LayoutCRecordSemantics@L27475`, `req.LayoutCEnumSemantics@L27492`, `req.LayoutExplicitEnumDiscriminant@L27509`, `req.LayoutPackedRecordSemantics@L27528`, `req.PackedFieldReferenceRequiresUnsafe@L27545`, `req.LayoutAlignSemantics@L27561`, `def.ValidLayoutAttributeCombinations@L27580`, `def.InvalidLayoutAttributeCombinations@L27600`
- `def.LayoutAttributeApplicability@L27615`, `req.LayoutAttributeConstraints@L27633`

#### `checker.attributes.optimization`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27688-L27708.

- `req.InlineAttributeSemantics@L27762`, `req.ColdAttributeSemantics@L27782`
- `req.InlineAttributeSemantics@L27762`, `req.ColdAttributeSemantics@L27782`

#### `checker.attributes.metadata`

Count: 23 total; 23 required; 0 recommended; 0 informative. Ledger line span: L27837-L28187.

- `def.DynamicDeclarationPredicate@L27911`, `def.DynamicExpressionPredicate@L27925`, `def.DynamicScopePredicate@L27939`, `def.InDynamicContext@L27953`, `req.DeprecatedAttributeSemantics@L27969`, `req.DynamicAttributeSemantics@L27983`, `req.DynamicScopeDetermination@L27997`, `def.ComputeDynamicContext@L28013`
- `def.FindInnermostDynamic@L28036`, `def.MinimalSpan@L28057`, `DynamicContext-Override@L28075`, `req.DynamicContextOverridePropagation@L28095`, `DynamicContext-NoInherit-Call@L28109`, `req.DynamicContextLexicalNoCallPropagation@L28129`, `req.DynamicEffectsAndRestrictions@L28143`, `req.DynamicTargetRestrictions@L28160`
- `req.EmptyDynamicScopeWarning@L28177`, `req.StaleOkAttributeSemantics@L28191`, `req.VerificationModeAttributeSemantics@L28205`, `req.ReflectAttributeSemantics@L28219`, `req.DeriveAttributeSemantics@L28233`, `req.EmitAttributeSemantics@L28247`, `req.FilesAttributeSemantics@L28261`
- `def.DynamicDeclarationPredicate@L27911`, `def.DynamicExpressionPredicate@L27925`, `def.DynamicScopePredicate@L27939`, `def.InDynamicContext@L27953`, `req.DeprecatedAttributeSemantics@L27969`, `req.DynamicAttributeSemantics@L27983`, `req.DynamicScopeDetermination@L27997`, `def.ComputeDynamicContext@L28013`
- `def.FindInnermostDynamic@L28036`, `def.MinimalSpan@L28057`, `DynamicContext-Override@L28075`, `req.DynamicContextOverridePropagation@L28095`, `DynamicContext-NoInherit-Call@L28109`, `req.DynamicContextLexicalNoCallPropagation@L28129`, `req.DynamicEffectsAndRestrictions@L28143`, `req.DynamicTargetRestrictions@L28160`
- `req.EmptyDynamicScopeWarning@L28177`, `req.StaleOkAttributeSemantics@L28191`, `req.VerificationModeAttributeSemantics@L28205`, `req.ReflectAttributeSemantics@L28219`, `req.DeriveAttributeSemantics@L28233`, `req.EmitAttributeSemantics@L28247`, `req.FilesAttributeSemantics@L28261`

#### `checker.permissions`

Count: 32 total; 32 required; 0 recommended; 0 informative. Ledger line span: L28361-L29129.

- `def.PermissionQualifierSemantics@L28720`, `req.PermissionRegimesDistinct@L28734`, `def.PermissionRegimeProperties@L28748`, `req.PermissionRegimeConstraints@L28768`, `def.SharedPermissionOperationMatrix@L28786`, `Layout-Perm@L28844`, `SizeOf-Perm@L28860`, `AlignOf-Perm@L28876`
- `conformance.AliasAndExclusivityRules@L28925`, `def.AliasingByOverlappingStorage@L28977`, `req.UniqueExclusivityInvariant@L28993`, `def.PermissionCoexistenceMatrix@L29009`, `req.BindingActivityNoConcreteSyntax@L29077`, `def.UniqueBindingActivityStates@L29127`, `Inactive-Enter@L29146`, `Inactive-Exit@L29164`
- `req.InactiveUniqueBindingNoDirectUse@L29182`, `req.BindingActivityNoAliasCreation@L29198`, `req.BindingActivityDeterministicReactivation@L29212`, `conformance.BindingActivityLowering@L29226`, `req.BindingActivityDiagnosticOwnership@L29242`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29261`, `def.PermissionAdmissibilityAstInputs@L29293`, `req.PermissionAdmissibilityScope@L29312`
- `def.PermAdmitsJudgementSet@L29328`, `def.PermissionAdmissibilityPairs@L29342`, `req.PermAdmitsUseSitesNoTypeRewrite@L29362`, `def.MethodReceiverPermissionAdmissibility@L29376`, `def.MethodReceiverPermissionMatrix@L29394`, `req.PermissionAdmissibilityNoImplicitConversion@L29412`, `conformance.PermissionAdmissibilitySharedKeyGate@L29458`, `diagnostics.PermissionAdmissibility@L29488`
- `def.PermissionQualifierSemantics@L28720`, `req.PermissionRegimesDistinct@L28734`, `def.PermissionRegimeProperties@L28748`, `req.PermissionRegimeConstraints@L28768`, `def.SharedPermissionOperationMatrix@L28786`, `Layout-Perm@L28844`, `SizeOf-Perm@L28860`, `AlignOf-Perm@L28876`
- `conformance.AliasAndExclusivityRules@L28925`, `def.AliasingByOverlappingStorage@L28977`, `req.UniqueExclusivityInvariant@L28993`, `def.PermissionCoexistenceMatrix@L29009`, `req.BindingActivityNoConcreteSyntax@L29077`, `def.UniqueBindingActivityStates@L29127`, `Inactive-Enter@L29146`, `Inactive-Exit@L29164`
- `req.InactiveUniqueBindingNoDirectUse@L29182`, `req.BindingActivityNoAliasCreation@L29198`, `req.BindingActivityDeterministicReactivation@L29212`, `conformance.BindingActivityLowering@L29226`, `req.BindingActivityDiagnosticOwnership@L29242`, `req.PermissionAdmissibilityNoAdditionalSyntax@L29261`, `def.PermissionAdmissibilityAstInputs@L29293`, `req.PermissionAdmissibilityScope@L29312`
- `def.PermAdmitsJudgementSet@L29328`, `def.PermissionAdmissibilityPairs@L29342`, `req.PermAdmitsUseSitesNoTypeRewrite@L29362`, `def.MethodReceiverPermissionAdmissibility@L29376`, `def.MethodReceiverPermissionMatrix@L29394`, `req.PermissionAdmissibilityNoImplicitConversion@L29412`, `conformance.PermissionAdmissibilitySharedKeyGate@L29458`, `diagnostics.PermissionAdmissibility@L29488`

#### `checker.ffi`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L31062-L31062.

- `req.ExternBlockDiagnosticOwnership@L31421`
- `req.ExternBlockDiagnosticOwnership@L31421`

#### `checker.types.primitive`

Count: 13 total; 13 required; 0 recommended; 0 informative. Ledger line span: L34345-L34624.

- `Parse-Prim-Type@L34704`, `Parse-Unit-Type@L34722`, `Parse-Never-Type@L34740`, `def.PrimitiveTypeName@L34758`, `def.TypePrimAst@L34774`, `def.NumericPrimitiveTypeSets@L34788`, `def.TypeWFJudgementSet@L34806`, `WF-Prim@L34822`
- `def.PrimitiveFloatRepresentation@L34840`, `def.DefaultNumericTypes@L34863`, `def.PrimitiveIntegerWidths@L34878`, `def.PrimitiveRangeOf@L34894`, `req.PrimitiveTypeDiagnosticOwnership@L34983`
- `Parse-Prim-Type@L34704`, `Parse-Unit-Type@L34722`, `Parse-Never-Type@L34740`, `def.PrimitiveTypeName@L34758`, `def.TypePrimAst@L34774`, `def.NumericPrimitiveTypeSets@L34788`, `def.TypeWFJudgementSet@L34806`, `WF-Prim@L34822`
- `def.PrimitiveFloatRepresentation@L34840`, `def.DefaultNumericTypes@L34863`, `def.PrimitiveIntegerWidths@L34878`, `def.PrimitiveRangeOf@L34894`, `req.PrimitiveTypeDiagnosticOwnership@L34983`

#### `checker.types.tuples`

Count: 41 total; 41 required; 0 recommended; 0 informative. Ledger line span: L34683-L35482.

- `Parse-Tuple-Type@L35042`, `Parse-TupleTypeElems-Empty@L35060`, `Parse-TupleTypeElems-One@L35078`, `Parse-TupleTypeElems-Many@L35096`, `def.TupleScanDelimiterDeltas@L35114`, `TupleScan-EOF@L35172`, `TupleScan-EndParen@L35190`, `TupleScan-SingletonComma@L35207`
- `TupleScan-Separator@L35224`, `TupleScan-Advance@L35241`, `def.TupleParen@L35258`, `Parse-Tuple-Literal@L35272`, `Parse-TupleExprElems-Empty@L35290`, `Parse-TupleExprElems-Single@L35308`, `Parse-TupleExprElems-Many@L35326`, `Postfix-TupleIndex@L35344`
- `def.TupleTypeAst@L35362`, `def.TupleExpressionAst@L35378`, `WF-Tuple@L35409`, `T-Tuple-Unit@L35427`, `T-Tuple@L35444`, `T-Tuple-Index@L35462`, `T-Tuple-Index-Perm@L35480`, `P-Tuple-Index@L35498`
- `P-Tuple-Index-Perm@L35516`, `def.ConstTupleIndex@L35534`, `TupleIndex-NonConst@L35548`, `TupleIndex-OOB@L35566`, `TupleAccess-NotTuple@L35584`, `req.TuplePatternRulesOwnership@L35602`, `EvalSigma-Tuple@L35632`, `EvalSigma-Tuple-Ctrl@L35650`
- `EvalSigma-TupleAccess@L35668`, `EvalSigma-TupleAccess-Ctrl@L35686`, `Layout-Tuple-Empty@L35720`, `Layout-Tuple-Cons@L35737`, `Size-Tuple@L35755`, `Align-Tuple@L35773`, `Layout-Tuple@L35791`, `Lower-Expr-Tuple@L35823`
- `diagnostics.Tuples@L35841`
- `Parse-Tuple-Type@L35042`, `Parse-TupleTypeElems-Empty@L35060`, `Parse-TupleTypeElems-One@L35078`, `Parse-TupleTypeElems-Many@L35096`, `def.TupleScanDelimiterDeltas@L35114`, `TupleScan-EOF@L35172`, `TupleScan-EndParen@L35190`, `TupleScan-SingletonComma@L35207`
- `TupleScan-Separator@L35224`, `TupleScan-Advance@L35241`, `def.TupleParen@L35258`, `Parse-Tuple-Literal@L35272`, `Parse-TupleExprElems-Empty@L35290`, `Parse-TupleExprElems-Single@L35308`, `Parse-TupleExprElems-Many@L35326`, `Postfix-TupleIndex@L35344`
- `def.TupleTypeAst@L35362`, `def.TupleExpressionAst@L35378`, `WF-Tuple@L35409`, `T-Tuple-Unit@L35427`, `T-Tuple@L35444`, `T-Tuple-Index@L35462`, `T-Tuple-Index-Perm@L35480`, `P-Tuple-Index@L35498`
- `P-Tuple-Index-Perm@L35516`, `def.ConstTupleIndex@L35534`, `TupleIndex-NonConst@L35548`, `TupleIndex-OOB@L35566`, `TupleAccess-NotTuple@L35584`, `req.TuplePatternRulesOwnership@L35602`, `EvalSigma-Tuple@L35632`, `EvalSigma-Tuple-Ctrl@L35650`
- `EvalSigma-TupleAccess@L35668`, `EvalSigma-TupleAccess-Ctrl@L35686`, `Layout-Tuple-Empty@L35720`, `Layout-Tuple-Cons@L35737`, `Size-Tuple@L35755`, `Align-Tuple@L35773`, `Layout-Tuple@L35791`, `Lower-Expr-Tuple@L35823`
- `diagnostics.Tuples@L35841`

#### `checker.types.arrays`

Count: 34 total; 34 required; 0 recommended; 0 informative. Ledger line span: L35530-L36185.

- `Parse-Array-Type@L35889`, `Parse-Array-Segment-Elem@L35907`, `Parse-Array-Segment-Repeat@L35925`, `Parse-Array-Segment-List-Empty@L35943`, `Parse-Array-Segment-List-Single@L35960`, `Parse-Array-Segment-List-Comma@L35978`, `Parse-Array-Literal@L35996`, `Postfix-Index@L36014`
- `def.ArrayAstForms@L36032`, `req.IndexAccessArrayOwnership@L36050`, `def.ConstIndex@L36064`, `WF-Array@L36080`, `def.ArraySegmentLength@L36098`, `T-Array-Literal-Segments@L36113`, `T-Index-Array@L36139`, `T-Index-Array-Dynamic@L36157`
- `T-Index-Array-Perm@L36175`, `T-Index-Array-Perm-Dynamic@L36193`, `P-Index-Array@L36211`, `P-Index-Array-Perm@L36229`, `P-Index-Array-Dynamic@L36247`, `P-Index-Array-Perm-Dynamic@L36265`, `Index-Array-NonConst-Err@L36283`, `Index-Array-OOB-Err@L36301`
- `Index-Array-NonUsize@L36319`, `EvalSigma-Array@L36370`, `EvalSigma-Array-Ctrl@L36388`, `EvalSigma-Index@L36406`, `EvalSigma-Index-OOB@L36424`, `Size-Array@L36444`, `Align-Array@L36462`, `Layout-Array@L36480`
- `Lower-Expr-Array@L36526`, `req.ArrayDiagnosticOwnership@L36544`
- `Parse-Array-Type@L35889`, `Parse-Array-Segment-Elem@L35907`, `Parse-Array-Segment-Repeat@L35925`, `Parse-Array-Segment-List-Empty@L35943`, `Parse-Array-Segment-List-Single@L35960`, `Parse-Array-Segment-List-Comma@L35978`, `Parse-Array-Literal@L35996`, `Postfix-Index@L36014`
- `def.ArrayAstForms@L36032`, `req.IndexAccessArrayOwnership@L36050`, `def.ConstIndex@L36064`, `WF-Array@L36080`, `def.ArraySegmentLength@L36098`, `T-Array-Literal-Segments@L36113`, `T-Index-Array@L36139`, `T-Index-Array-Dynamic@L36157`
- `T-Index-Array-Perm@L36175`, `T-Index-Array-Perm-Dynamic@L36193`, `P-Index-Array@L36211`, `P-Index-Array-Perm@L36229`, `P-Index-Array-Dynamic@L36247`, `P-Index-Array-Perm-Dynamic@L36265`, `Index-Array-NonConst-Err@L36283`, `Index-Array-OOB-Err@L36301`
- `Index-Array-NonUsize@L36319`, `EvalSigma-Array@L36370`, `EvalSigma-Array-Ctrl@L36388`, `EvalSigma-Index@L36406`, `EvalSigma-Index-OOB@L36424`, `Size-Array@L36444`, `Align-Array@L36462`, `Layout-Array@L36480`
- `Lower-Expr-Array@L36526`, `req.ArrayDiagnosticOwnership@L36544`

#### `checker.types.slices`

Count: 28 total; 28 required; 0 recommended; 0 informative. Ledger line span: L36224-L36756.

- `req.ArrayToSliceCoercionSemantic@L36583`, `Parse-Slice-Type@L36599`, `req.IndexAccessSliceOwnership@L36617`, `def.TypeSliceAst@L36631`, `req.IndexAccessSliceExpressionSemantics@L36647`, `def.RangeIndexType@L36661`, `WF-Slice@L36677`, `T-Index-Slice@L36695`
- `T-Index-Slice-Perm@L36713`, `T-Slice-From-Array@L36731`, `T-Slice-From-Array-Perm@L36749`, `T-Slice-From-Slice@L36767`, `T-Slice-From-Slice-Perm@L36785`, `P-Index-Slice@L36803`, `P-Index-Slice-Perm@L36821`, `P-Slice-From-Array@L36839`
- `P-Slice-From-Array-Perm@L36857`, `P-Slice-From-Slice@L36875`, `P-Slice-From-Slice-Perm@L36893`, `Coerce-Array-Slice@L36911`, `Index-NonIndexable@L36929`, `EvalSigma-Index-Range@L36978`, `EvalSigma-Index-Range-OOB@L36996`, `Size-Slice@L37016`
- `Align-Slice@L37033`, `Layout-Slice@L37050`, `Index-Slice-NonUsize@L37097`, `req.SliceDiagnosticOwnership@L37115`
- `req.ArrayToSliceCoercionSemantic@L36583`, `Parse-Slice-Type@L36599`, `req.IndexAccessSliceOwnership@L36617`, `def.TypeSliceAst@L36631`, `req.IndexAccessSliceExpressionSemantics@L36647`, `def.RangeIndexType@L36661`, `WF-Slice@L36677`, `T-Index-Slice@L36695`
- `T-Index-Slice-Perm@L36713`, `T-Slice-From-Array@L36731`, `T-Slice-From-Array-Perm@L36749`, `T-Slice-From-Slice@L36767`, `T-Slice-From-Slice-Perm@L36785`, `P-Index-Slice@L36803`, `P-Index-Slice-Perm@L36821`, `P-Slice-From-Array@L36839`
- `P-Slice-From-Array-Perm@L36857`, `P-Slice-From-Slice@L36875`, `P-Slice-From-Slice-Perm@L36893`, `Coerce-Array-Slice@L36911`, `Index-NonIndexable@L36929`, `EvalSigma-Index-Range@L36978`, `EvalSigma-Index-Range-OOB@L36996`, `Size-Slice@L37016`
- `Align-Slice@L37033`, `Layout-Slice@L37050`, `Index-Slice-NonUsize@L37097`, `req.SliceDiagnosticOwnership@L37115`

#### `checker.types.ranges`

Count: 55 total; 55 required; 0 recommended; 0 informative. Ledger line span: L36813-L37833.

- `Parse-Range-To@L37172`, `Parse-Range-ToInc@L37190`, `Parse-Range-Full@L37208`, `Parse-Range-Lhs@L37226`, `Parse-RangeTail-None@L37244`, `Parse-RangeTail-From@L37262`, `Parse-RangeTail-Exclusive@L37280`, `Parse-RangeTail-Inclusive@L37298`
- `def.RangeSurfaceTypeElaboration@L37316`, `def.RangeTypeAst@L37337`, `def.RangeExprAst@L37353`, `def.IsRangeType@L37367`, `def.RangeFullExprType@L37381`, `def.RangeToExprType@L37397`, `def.RangeToInclusiveExprType@L37411`, `def.RangeFromExprType@L37425`
- `def.RangeExclusiveExprType@L37439`, `def.RangeInclusiveExprType@L37453`, `T-Range-Lift@L37467`, `Range-Full@L37485`, `Range-To@L37502`, `Range-ToInclusive@L37520`, `Range-From@L37538`, `Range-Exclusive@L37556`
- `Range-Inclusive@L37574`, `req.RangePatternSemanticsOwnership@L37592`, `EvalSigma-Range@L37627`, `EvalSigma-Range-Ctrl@L37645`, `EvalSigma-Range-Ctrl-Hi@L37663`, `def.RangeInc@L37681`, `Lower-Range-Full@L37764`, `Lower-Range-To@L37781`
- `Lower-Range-ToInclusive@L37799`, `Lower-Range-From@L37817`, `Lower-Range-Inclusive@L37835`, `Lower-Range-Exclusive@L37853`, `Size-Range@L37871`, `Align-Range@L37889`, `Layout-Range@L37907`, `Size-RangeInclusive@L37925`
- `Align-RangeInclusive@L37943`, `Layout-RangeInclusive@L37961`, `Size-RangeFrom@L37979`, `Align-RangeFrom@L37997`, `Layout-RangeFrom@L38015`, `Size-RangeTo@L38033`, `Align-RangeTo@L38051`, `Layout-RangeTo@L38069`
- `Size-RangeToInclusive@L38087`, `Align-RangeToInclusive@L38105`, `Layout-RangeToInclusive@L38123`, `Size-RangeFull@L38141`, `Align-RangeFull@L38158`, `Layout-RangeFull@L38175`, `req.RangeDiagnosticOwnership@L38192`
- `Parse-Range-To@L37172`, `Parse-Range-ToInc@L37190`, `Parse-Range-Full@L37208`, `Parse-Range-Lhs@L37226`, `Parse-RangeTail-None@L37244`, `Parse-RangeTail-From@L37262`, `Parse-RangeTail-Exclusive@L37280`, `Parse-RangeTail-Inclusive@L37298`
- `def.RangeSurfaceTypeElaboration@L37316`, `def.RangeTypeAst@L37337`, `def.RangeExprAst@L37353`, `def.IsRangeType@L37367`, `def.RangeFullExprType@L37381`, `def.RangeToExprType@L37397`, `def.RangeToInclusiveExprType@L37411`, `def.RangeFromExprType@L37425`
- `def.RangeExclusiveExprType@L37439`, `def.RangeInclusiveExprType@L37453`, `T-Range-Lift@L37467`, `Range-Full@L37485`, `Range-To@L37502`, `Range-ToInclusive@L37520`, `Range-From@L37538`, `Range-Exclusive@L37556`
- `Range-Inclusive@L37574`, `req.RangePatternSemanticsOwnership@L37592`, `EvalSigma-Range@L37627`, `EvalSigma-Range-Ctrl@L37645`, `EvalSigma-Range-Ctrl-Hi@L37663`, `def.RangeInc@L37681`, `Lower-Range-Full@L37764`, `Lower-Range-To@L37781`
- `Lower-Range-ToInclusive@L37799`, `Lower-Range-From@L37817`, `Lower-Range-Inclusive@L37835`, `Lower-Range-Exclusive@L37853`, `Size-Range@L37871`, `Align-Range@L37889`, `Layout-Range@L37907`, `Size-RangeInclusive@L37925`
- `Align-RangeInclusive@L37943`, `Layout-RangeInclusive@L37961`, `Size-RangeFrom@L37979`, `Align-RangeFrom@L37997`, `Layout-RangeFrom@L38015`, `Size-RangeTo@L38033`, `Align-RangeTo@L38051`, `Layout-RangeTo@L38069`
- `Size-RangeToInclusive@L38087`, `Align-RangeToInclusive@L38105`, `Layout-RangeToInclusive@L38123`, `Size-RangeFull@L38141`, `Align-RangeFull@L38158`, `Layout-RangeFull@L38175`, `req.RangeDiagnosticOwnership@L38192`

#### `spec.modal-special`

Count: 386 total; 386 required; 0 recommended; 0 informative. Ledger line span: L40979-L46752.

- `grammar.ModalDeclarations.Syntax@L41338`, `rule.13.Parse-Modal@L41360`, `rule.13.Parse-ModalBody@L41376`, `rule.13.Parse-StateBlock@L41392`, `def.ModalTypeRef.Parser@L41410`, `rule.13.Parse-Modal-State-Type@L41422`, `rule.13.Parse-Record-Literal-ModalState@L41438`, `req.ModalStateMemberDispatch@L41454`
- `def.ModalDeclAst@L41468`, `def.StateBlockAst@L41481`, `def.StateMemberAst@L41493`, `def.ModalRefAst@L41509`, `def.TypeRefModalStateAst@L41521`, `def.ModalStateAccessors@L41533`, `def.ModalStatePayload@L41550`, `def.BuiltinModalSet@L41562`
- `def.ModalPath@L41574`, `def.ModalSelfRef@L41587`, `def.ModalSelfTypes@L41602`, `def.ModalRefAccessors@L41615`, `def.ModalDeclOf@L41632`, `def.ModalRefSubst@L41644`, `def.ModalPayloadSubstitution@L41656`, `def.PayloadMap@L41668`
- `def.ModalPayloadMap@L41682`, `rule.13.WF-Modal-Payload@L41698`, `rule.13.Modal-Payload-DupField@L41714`, `rule.13.WF-ModalState@L41730`, `rule.13.WF-ModalState-ArgCount-Err@L41746`, `def.StateMemberVisOk@L41762`, `rule.13.WF-ModalDecl@L41774`, `rule.13.StateMemberVisOk-Err@L41790`
- `rule.13.Modal-WF@L41806`, `rule.13.Modal-NoStates-Err@L41822`, `rule.13.Modal-DupState-Err@L41838`, `rule.13.Modal-StateName-Err@L41854`, `rule.13.State-Specific-WF@L41870`, `def.ModalPayloadNames@L41886`, `rule.13.T-Modal-State-Intro@L41899`, `rule.13.Record-FileDir-Err@L41915`
- `def.RegionPayload@L41933`, `def.RegionProcSet@L41948`, `def.RegionNewScopedProcSig@L41960`, `def.RegionAllocProcSig@L41972`, `def.RegionResetUncheckedProcSig@L41984`, `def.RegionFreezeProcSig@L41996`, `def.RegionThawProcSig@L42008`, `def.RegionFreeUncheckedProcSig@L42020`
- `def.RegionProvenanceTypeHelpers@L42032`, `def.RegionNonBitcopy@L42046`, `req.RegionAllocProvenance@L42060`, `req.RegionInactiveDereferenceSemantics@L42072`, `req.RegionFreeAtScopeExit@L42084`, `rule.13.Region-Unchecked-Unsafe-Err@L42096`, `def.CancelTokenTypePresence@L42112`, `def.CancelTokenPayload@L42125`
- `def.CancelTokenMembersAndDecl@L42138`, `def.CancelTokenTypeBinding@L42160`, `def.CancelTokenProcSignatures@L42172`, `def.SpawnedTypePresence@L42185`, `def.SpawnedPayload@L42198`, `def.SpawnedMembersAndDecl@L42214`, `def.SpawnedTypeBinding@L42234`, `def.TrackedTypePresence@L42246`
- `def.TrackedPayload@L42259`, `def.TrackedMembersAndDecl@L42275`, `def.TrackedTypeBinding@L42295`, `def.DirIterMembersAndDecl@L42307`, `def.FileMembersAndDecl@L42329`, `def.DirIterAndFileTypeBindings@L42366`, `req.AsyncModalDefinedInChapter21@L42379`, `def.ModalValRuntime@L42393`
- `def.RecordModalStateValueType@L42405`, `def.ModalValValueType@L42417`, `req.ModalRuntimeRepresentation@L42429`, `def.ModalDiscType@L42443`, `def.ModalStateLayoutMetrics@L42455`, `def.ModalSingleFieldPayload@L42468`, `def.ModalEmptyState@L42481`, `def.ModalPayloadState@L42493`
- `def.ModalNicheApplies@L42505`, `def.ModalStateValueBits@L42517`, `def.ModalEmptyStates@L42529`, `def.EmptyRecordVal@L42541`, `def.ModalNicheBits@L42553`, `def.ModalBits@L42565`, `def.ModalAlign@L42577`, `def.ModalSize@L42589`
- `def.ModalPayloadSize@L42601`, `def.ModalPayloadAlign@L42613`, `def.StateRecordBits@L42625`, `def.ModalPayloadBits@L42637`, `def.ModalLayoutJudgementSet@L42649`, `rule.13.Layout-Modal-Niche@L42661`, `rule.13.Layout-Modal-Tagged@L42677`, `rule.13.Size-Modal@L42693`
- `rule.13.Align-Modal@L42709`, `rule.13.Layout-Modal@L42725`, `rule.13.Size-ModalState@L42741`, `rule.13.Align-ModalState@L42757`, `rule.13.Layout-ModalState@L42773`, `def.ModalStateLayoutEquation@L42789`, `def.EmptyModalStateSizeEquation@L42801`, `def.ModalBaseLayoutEquation@L42813`
- `req.ModalTaggedPaddingZero@L42827`, `def.ModalTaggedBits@L42838`, `def.ModalRefValueBits@L42850`, `diag.ModalDeclarations@L42864`, `grammar.StateFields.Syntax@L42880`, `rule.13.Parse-StateMember-Field@L42896`, `def.StateFieldDeclAst@L42914`, `def.PayloadNameHelpers@L42926`
- `def.ModalFieldVisible@L42941`, `rule.13.T-Modal-Field@L42953`, `rule.13.T-Modal-Field-Perm@L42969`, `rule.13.Modal-Field-Missing@L42985`, `rule.13.Modal-Field-General-Err@L43001`, `rule.13.Modal-Field-NotVisible@L43017`, `req.StateFieldDynamic@L43035`, `req.StateFieldLowering@L43049`
- `diag.StateFields@L43063`, `grammar.StateMethods.Syntax@L43079`, `rule.13.Parse-StateMember-Method@L43096`, `req.StateMethodSignatureParser@L43112`, `def.StateMethodDeclAst@L43126`, `def.StateMethodCollections@L43138`, `def.StateMethodSig@L43151`, `def.LookupStateMethod@L43165`
- `def.StateMemberVisible@L43180`, `rule.13.StateMethod-Dup@L43192`, `rule.13.WF-State-Method@L43208`, `rule.13.T-Modal-Method@L43224`, `rule.13.Modal-Method-RecvPerm-Err@L43240`, `rule.13.Modal-Method-NotFound@L43256`, `rule.13.Modal-Method-NotVisible@L43272`, `rule.13.T-Modal-Method-Body@L43288`
- `def.StateMethodTarget@L43306`, `rule.13.ApplyMethodSigma@L43318`, `req.BuiltinStateMethodCalling@L43334`, `req.StateMethodLowering@L43348`, `diag.StateMethods@L43362`, `grammar.Transitions.Syntax@L43378`, `rule.13.Parse-StateMember-Transition@L43394`, `def.TransitionDeclAst@L43412`
- `def.TransitionCollections@L43424`, `def.LookupTransition@L43438`, `def.TransitionSig@L43451`, `rule.13.Transition-Dup@L43470`, `rule.13.StateMember-Name-Conflict@L43486`, `rule.13.WF-Transition@L43502`, `rule.13.Transition-Target-Err@L43518`, `rule.13.T-Modal-Transition@L43534`
- `rule.13.Transition-Source-Err@L43550`, `rule.13.Transition-NotVisible@L43566`, `rule.13.T-Modal-Transition-Body@L43582`, `rule.13.Transition-Body-Err@L43598`, `def.TransitionMethodTarget@L43616`, `req.TransitionRuntimeSemantics@L43628`, `def.IsTransition@L43640`, `def.TransitionTarget@L43652`
- `rule.13.ApplyTransitionSigma@L43664`, `def.ExtractReturnValue@L43682`, `def.ValidateModalState@L43695`, `req.TransitionLowering@L43709`, `diag.Transitions@L43723`, `grammar.ModalWidening.Syntax@L43739`, `rule.13.Parse-Unary-Widen@L43755`, `def.ModalWidening.AST@L43773`
- `def.ModalWideningThreshold@L43787`, `rule.13.T-Modal-Widen@L43799`, `rule.13.T-Modal-Widen-Perm@L43815`, `rule.13.Widen-AlreadyGeneral@L43831`, `rule.13.Widen-NonModal@L43847`, `def.NicheCompatible@L43863`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43875`, `def.WidenWarnCond@L43891`
- `rule.13.Warn-Widen-LargePayload@L43903`, `rule.13.Warn-Widen-Ok@L43919`, `def.ModalWideningDynamic@L43937`, `req.ModalWideningLowering@L43951`, `def.ModalStateSizeBound@L43963`, `diag.ModalWidening@L43977`, `grammar.StringTypes.Syntax@L43993`, `rule.13.Parse-String-Type@L44010`
- `rule.13.Parse-StringState-None@L44028`, `rule.13.Parse-StringState-Managed@L44044`, `rule.13.Parse-StringState-View@L44060`, `def.TypeStringAst@L44078`, `def.StringStateSet@L44090`, `def.StringBuiltinTable@L44102`, `def.StringBuiltinSig@L44124`, `rule.13.WF-String@L44138`
- `rule.13.Sub-String-State@L44154`, `req.StringBuiltinsTyping@L44168`, `def.StringLiteralVal@L44182`, `def.StringBytesStoreDomains@L44194`, `def.ViewBytes@L44210`, `def.ByteSeqOf@L44222`, `def.ByteLen@L44237`, `def.StringValueTypes@L44249`
- `def.StringBytesJudgementSet@L44982`, `req.StringLiteralStorage@L44284`, `rule.13.StringFrom-Ok@L44298`, `rule.13.StringFrom-Err@L44314`, `rule.13.StringAsView-Ok@L44330`, `rule.13.StringToManaged-Ok@L44362`, `rule.13.StringToManaged-Err@L44378`, `rule.13.StringCloneWith-Ok@L44394`
- `rule.13.StringCloneWith-Err@L44410`, `rule.13.StringAppend-Ok@L44426`, `rule.13.StringAppend-Err@L44442`, `rule.13.StringLength@L44458`, `rule.13.StringIsEmpty@L44474`, `def.StringViewOf@L44490`, `def.StringRuntimeLength@L44504`, `def.StringManagedLoweringLayout@L44519`
- `def.StringViewLoweringLayout@L44533`, `rule.13.Size-String-Managed@L44547`, `rule.13.Align-String-Managed@L44563`, `rule.13.Layout-String-Managed@L44579`, `rule.13.Size-String-View@L44595`, `rule.13.Align-String-View@L44611`, `rule.13.Layout-String-View@L44627`, `rule.13.Size-String-Modal@L44643`
- `rule.13.Align-String-Modal@L44659`, `def.StringValueBits@L44675`, `def.DropManagedString@L44687`, `diag.StringTypes@L44701`, `grammar.BytesTypes.Syntax@L44717`, `rule.13.Parse-Bytes-Type@L44734`, `rule.13.Parse-BytesState-None@L44752`, `rule.13.Parse-BytesState-Managed@L44768`
- `rule.13.Parse-BytesState-View@L44784`, `def.TypeBytesAst@L44802`, `def.BytesStateSet@L44814`, `def.BytesBuiltinTable@L44826`, `def.StringBytesBuiltinTable@L44850`, `def.BytesBuiltinSig@L44862`, `def.StringBytesBuiltinSig@L44874`, `rule.13.WF-Bytes@L44889`
- `rule.13.Sub-Bytes-State@L44905`, `req.BytesBuiltinsTyping@L44919`, `def.SliceBytes@L44933`, `def.BytesValueTypes@L44945`, `def.BytesJudgementSet@L44959`, `def.StringBytesJudgementSet@L44982`, `rule.13.BytesWithCapacity-Ok@L44994`, `rule.13.BytesWithCapacity-Err@L45010`
- `rule.13.BytesFromSlice-Ok@L45026`, `rule.13.BytesFromSlice-Err@L45042`, `rule.13.BytesAsView-Ok@L45058`, `rule.13.BytesToManaged-Ok@L45074`, `rule.13.BytesToManaged-Err@L45090`, `rule.13.BytesView-Ok@L45106`, `rule.13.BytesViewString-Ok@L45122`, `rule.13.BytesAsSlice-Ok@L45138`
- `rule.13.BytesAppend-Ok@L45154`, `rule.13.BytesAppend-Err@L45170`, `rule.13.BytesLength@L45186`, `rule.13.BytesIsEmpty@L45202`, `def.BytesViewOf@L45218`, `def.BytesRuntimeLength@L45232`, `def.BytesViewConversions@L45245`, `def.BytesManagedLoweringLayout@L45260`
- `def.BytesViewLoweringLayout@L45274`, `rule.13.Size-Bytes-Managed@L45288`, `rule.13.Align-Bytes-Managed@L45304`, `rule.13.Layout-Bytes-Managed@L45320`, `rule.13.Size-Bytes-View@L45336`, `rule.13.Align-Bytes-View@L45352`, `rule.13.Layout-Bytes-View@L45368`, `rule.13.Size-Bytes-Modal@L45384`
- `rule.13.Align-Bytes-Modal@L45400`, `def.BytesValueBits@L45416`, `def.DropManagedBytes@L45428`, `diag.BytesTypes@L45442`, `grammar.SafePointerTypes.Syntax@L45458`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45475`, `rule.13.Parse-Safe-Pointer-Type@L45491`, `rule.13.Parse-PtrState-None@L45509`
- `rule.13.Parse-PtrState-Valid@L45525`, `rule.13.Parse-PtrState-Null@L45541`, `rule.13.Parse-PtrState-Expired@L45557`, `def.PtrStateSet@L45575`, `def.SafePointerTypeForms@L45587`, `rule.13.WF-Ptr@L45602`, `def.SafePointerTraits@L45618`, `rule.13.Sub-Ptr-State@L45632`
- `def.SafePointerRuntimeConstructors@L45650`, `def.SafePointerValueType@L45664`, `def.PtrStateImmediate@L45676`, `def.PtrStateValid@L45689`, `def.PtrAddrJudgementSet@L45704`, `rule.13.ReadPtr-Safe@L45716`, `rule.13.WritePtr-Safe@L45732`, `rule.13.ReadPtr-Null@L45748`
- `rule.13.ReadPtr-Expired@L45764`, `rule.13.WritePtr-Null@L45780`, `rule.13.WritePtr-Expired@L45796`, `rule.13.Size-Ptr@L45814`, `rule.13.Align-Ptr@L45830`, `rule.13.Layout-Ptr@L45846`, `def.SafePointerSizeAlignEquations@L45862`, `def.PtrDiagRefs@L45874`
- `def.SafePointerNicheSet@L45886`, `def.SafePointerValidValue@L45899`, `def.SafePointerValueBits@L45914`, `diag.SafePointerTypes@L45931`, `grammar.RawPointerTypes.Syntax@L45947`, `rule.13.Parse-Raw-Pointer-Type@L45963`, `def.RawPointerTypes.AST@L45981`, `rule.13.WF-RawPtr@L45995`
- `rule.13.T-Deref-Raw@L46011`, `rule.13.P-Deref-Raw-Imm@L46027`, `rule.13.P-Deref-Raw-Mut@L46043`, `rule.13.Deref-Raw-Unsafe@L46059`, `def.RawPointerRuntimeValue@L46077`, `rule.13.ReadPtr-Raw@L46089`, `rule.13.WritePtr-Raw@L46105`, `rule.13.ReadPtr-Raw-Invalid@L46121`
- `rule.13.WritePtr-Raw-Imm@L46137`, `rule.13.WritePtr-Raw-Invalid@L46153`, `rule.13.Size-RawPtr@L46171`, `rule.13.Align-RawPtr@L46187`, `rule.13.Layout-RawPtr@L46203`, `def.RawPointerValidValue@L46219`, `def.RawPointerValueBits@L46231`, `req.RawPointerLowering@L46242`
- `diag.RawPointerTypes@L46256`, `grammar.FunctionTypes.Syntax@L46272`, `req.FunctionTypeTrailingComma@L46288`, `rule.13.Parse-Func-Type@L46302`, `rule.13.Parse-ParamType-Move@L46318`, `rule.13.Parse-ParamType-Plain@L46334`, `rule.13.Parse-ParamTypeList-Empty@L46350`, `rule.13.Parse-ParamTypeList-Cons@L46366`
- `rule.13.Parse-ParamTypeListTail-End@L46382`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46398`, `rule.13.Parse-ParamTypeListTail-Cons@L46414`, `def.FunctionTypes.AST@L46432`, `rule.13.WF-Func@L46446`, `rule.13.T-Equiv-Func@L46462`, `rule.13.Sub-Func@L46478`, `rule.13.T-Proc-As-Value@L46494`
- `diag.FunctionTypeCalls@L46510`, `def.FunctionRuntimeValue@L46524`, `rule.13.EvalSigma-Call-Proc@L46536`, `req.NamedProceduresFirstClass@L46552`, `rule.13.Size-Func@L46566`, `rule.13.Align-Func@L46582`, `rule.13.Layout-Func@L46598`, `req.FunctionTypeCallLowering@L46614`
- `diag.FunctionTypes@L46628`, `grammar.ClosureTypes.Syntax@L46644`, `req.ClosureParamUnionParentheses@L46661`, `rule.13.Parse-Closure-Type@L46675`, `rule.13.Parse-Closure-Type-Empty@L46691`, `rule.13.Parse-ClosureParamType-Grouped@L46707`, `rule.13.Parse-ClosureParamType-Plain@L46723`, `rule.13.Parse-ClosureParamTypeList-Empty@L46739`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46755`, `rule.13.Parse-ClosureParamTypeListTail-End@L46771`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46787`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46803`, `rule.13.Parse-ClosureDepsOpt-None@L46819`, `rule.13.Parse-ClosureDepsOpt-Some@L46835`, `rule.13.Parse-SharedDepList-Empty@L46851`, `rule.13.Parse-SharedDepList-Single@L46867`
- `rule.13.Parse-SharedDepList-Cons@L46883`, `rule.13.Parse-SharedDep@L46899`, `def.TypeClosureAst@L46917`, `def.ClosureDepsOpt@L46929`, `req.ClosureTypeOwnershipBoundaries@L46941`, `rule.13.WF-Closure@L46955`, `rule.13.T-Equiv-Closure@L46971`, `rule.13.Sub-Closure@L46987`
- `req.ClosureExpressionOwnership@L47003`, `def.ClosureRuntimeValue@L47017`, `req.ClosureOperationOwnership@L47029`, `def.ClosureLoweringRep@L47043`, `rule.13.Size-Closure@L47055`, `rule.13.Align-Closure@L47071`, `rule.13.Layout-Closure@L47087`, `req.ClosureLoweringOwnership@L47103`
- `diag.ClosureTypes@L47117`, `diagnostics.ModalPointerSupplement@L47129`
- `grammar.ModalDeclarations.Syntax@L41338`, `rule.13.Parse-Modal@L41360`, `rule.13.Parse-ModalBody@L41376`, `rule.13.Parse-StateBlock@L41392`, `def.ModalTypeRef.Parser@L41410`, `rule.13.Parse-Modal-State-Type@L41422`, `rule.13.Parse-Record-Literal-ModalState@L41438`, `req.ModalStateMemberDispatch@L41454`
- `def.ModalDeclAst@L41468`, `def.StateBlockAst@L41481`, `def.StateMemberAst@L41493`, `def.ModalRefAst@L41509`, `def.TypeRefModalStateAst@L41521`, `def.ModalStateAccessors@L41533`, `def.ModalStatePayload@L41550`, `def.BuiltinModalSet@L41562`
- `def.ModalPath@L41574`, `def.ModalSelfRef@L41587`, `def.ModalSelfTypes@L41602`, `def.ModalRefAccessors@L41615`, `def.ModalDeclOf@L41632`, `def.ModalRefSubst@L41644`, `def.ModalPayloadSubstitution@L41656`, `def.PayloadMap@L41668`
- `def.ModalPayloadMap@L41682`, `rule.13.WF-Modal-Payload@L41698`, `rule.13.Modal-Payload-DupField@L41714`, `rule.13.WF-ModalState@L41730`, `rule.13.WF-ModalState-ArgCount-Err@L41746`, `def.StateMemberVisOk@L41762`, `rule.13.WF-ModalDecl@L41774`, `rule.13.StateMemberVisOk-Err@L41790`
- `rule.13.Modal-WF@L41806`, `rule.13.Modal-NoStates-Err@L41822`, `rule.13.Modal-DupState-Err@L41838`, `rule.13.Modal-StateName-Err@L41854`, `rule.13.State-Specific-WF@L41870`, `def.ModalPayloadNames@L41886`, `rule.13.T-Modal-State-Intro@L41899`, `rule.13.Record-FileDir-Err@L41915`
- `def.RegionPayload@L41933`, `def.RegionProcSet@L41948`, `def.RegionNewScopedProcSig@L41960`, `def.RegionAllocProcSig@L41972`, `def.RegionResetUncheckedProcSig@L41984`, `def.RegionFreezeProcSig@L41996`, `def.RegionThawProcSig@L42008`, `def.RegionFreeUncheckedProcSig@L42020`
- `def.RegionProvenanceTypeHelpers@L42032`, `def.RegionNonBitcopy@L42046`, `req.RegionAllocProvenance@L42060`, `req.RegionInactiveDereferenceSemantics@L42072`, `req.RegionFreeAtScopeExit@L42084`, `rule.13.Region-Unchecked-Unsafe-Err@L42096`, `def.CancelTokenTypePresence@L42112`, `def.CancelTokenPayload@L42125`
- `def.CancelTokenMembersAndDecl@L42138`, `def.CancelTokenTypeBinding@L42160`, `def.CancelTokenProcSignatures@L42172`, `def.SpawnedTypePresence@L42185`, `def.SpawnedPayload@L42198`, `def.SpawnedMembersAndDecl@L42214`, `def.SpawnedTypeBinding@L42234`, `def.TrackedTypePresence@L42246`
- `def.TrackedPayload@L42259`, `def.TrackedMembersAndDecl@L42275`, `def.TrackedTypeBinding@L42295`, `def.DirIterMembersAndDecl@L42307`, `def.FileMembersAndDecl@L42329`, `def.DirIterAndFileTypeBindings@L42366`, `req.AsyncModalDefinedInChapter21@L42379`, `def.ModalValRuntime@L42393`
- `def.RecordModalStateValueType@L42405`, `def.ModalValValueType@L42417`, `req.ModalRuntimeRepresentation@L42429`, `def.ModalDiscType@L42443`, `def.ModalStateLayoutMetrics@L42455`, `def.ModalSingleFieldPayload@L42468`, `def.ModalEmptyState@L42481`, `def.ModalPayloadState@L42493`
- `def.ModalNicheApplies@L42505`, `def.ModalStateValueBits@L42517`, `def.ModalEmptyStates@L42529`, `def.EmptyRecordVal@L42541`, `def.ModalNicheBits@L42553`, `def.ModalBits@L42565`, `def.ModalAlign@L42577`, `def.ModalSize@L42589`
- `def.ModalPayloadSize@L42601`, `def.ModalPayloadAlign@L42613`, `def.StateRecordBits@L42625`, `def.ModalPayloadBits@L42637`, `def.ModalLayoutJudgementSet@L42649`, `rule.13.Layout-Modal-Niche@L42661`, `rule.13.Layout-Modal-Tagged@L42677`, `rule.13.Size-Modal@L42693`
- `rule.13.Align-Modal@L42709`, `rule.13.Layout-Modal@L42725`, `rule.13.Size-ModalState@L42741`, `rule.13.Align-ModalState@L42757`, `rule.13.Layout-ModalState@L42773`, `def.ModalStateLayoutEquation@L42789`, `def.EmptyModalStateSizeEquation@L42801`, `def.ModalBaseLayoutEquation@L42813`
- `req.ModalTaggedPaddingZero@L42827`, `def.ModalTaggedBits@L42838`, `def.ModalRefValueBits@L42850`, `diag.ModalDeclarations@L42864`, `grammar.StateFields.Syntax@L42880`, `rule.13.Parse-StateMember-Field@L42896`, `def.StateFieldDeclAst@L42914`, `def.PayloadNameHelpers@L42926`
- `def.ModalFieldVisible@L42941`, `rule.13.T-Modal-Field@L42953`, `rule.13.T-Modal-Field-Perm@L42969`, `rule.13.Modal-Field-Missing@L42985`, `rule.13.Modal-Field-General-Err@L43001`, `rule.13.Modal-Field-NotVisible@L43017`, `req.StateFieldDynamic@L43035`, `req.StateFieldLowering@L43049`
- `diag.StateFields@L43063`, `grammar.StateMethods.Syntax@L43079`, `rule.13.Parse-StateMember-Method@L43096`, `req.StateMethodSignatureParser@L43112`, `def.StateMethodDeclAst@L43126`, `def.StateMethodCollections@L43138`, `def.StateMethodSig@L43151`, `def.LookupStateMethod@L43165`
- `def.StateMemberVisible@L43180`, `rule.13.StateMethod-Dup@L43192`, `rule.13.WF-State-Method@L43208`, `rule.13.T-Modal-Method@L43224`, `rule.13.Modal-Method-RecvPerm-Err@L43240`, `rule.13.Modal-Method-NotFound@L43256`, `rule.13.Modal-Method-NotVisible@L43272`, `rule.13.T-Modal-Method-Body@L43288`
- `def.StateMethodTarget@L43306`, `rule.13.ApplyMethodSigma@L43318`, `req.BuiltinStateMethodCalling@L43334`, `req.StateMethodLowering@L43348`, `diag.StateMethods@L43362`, `grammar.Transitions.Syntax@L43378`, `rule.13.Parse-StateMember-Transition@L43394`, `def.TransitionDeclAst@L43412`
- `def.TransitionCollections@L43424`, `def.LookupTransition@L43438`, `def.TransitionSig@L43451`, `rule.13.Transition-Dup@L43470`, `rule.13.StateMember-Name-Conflict@L43486`, `rule.13.WF-Transition@L43502`, `rule.13.Transition-Target-Err@L43518`, `rule.13.T-Modal-Transition@L43534`
- `rule.13.Transition-Source-Err@L43550`, `rule.13.Transition-NotVisible@L43566`, `rule.13.T-Modal-Transition-Body@L43582`, `rule.13.Transition-Body-Err@L43598`, `def.TransitionMethodTarget@L43616`, `req.TransitionRuntimeSemantics@L43628`, `def.IsTransition@L43640`, `def.TransitionTarget@L43652`
- `rule.13.ApplyTransitionSigma@L43664`, `def.ExtractReturnValue@L43682`, `def.ValidateModalState@L43695`, `req.TransitionLowering@L43709`, `diag.Transitions@L43723`, `grammar.ModalWidening.Syntax@L43739`, `rule.13.Parse-Unary-Widen@L43755`, `def.ModalWidening.AST@L43773`
- `def.ModalWideningThreshold@L43787`, `rule.13.T-Modal-Widen@L43799`, `rule.13.T-Modal-Widen-Perm@L43815`, `rule.13.Widen-AlreadyGeneral@L43831`, `rule.13.Widen-NonModal@L43847`, `def.NicheCompatible@L43863`, `rule.13.Chk-Subsumption-Modal-NonNiche@L43875`, `def.WidenWarnCond@L43891`
- `rule.13.Warn-Widen-LargePayload@L43903`, `rule.13.Warn-Widen-Ok@L43919`, `def.ModalWideningDynamic@L43937`, `req.ModalWideningLowering@L43951`, `def.ModalStateSizeBound@L43963`, `diag.ModalWidening@L43977`, `grammar.StringTypes.Syntax@L43993`, `rule.13.Parse-String-Type@L44010`
- `rule.13.Parse-StringState-None@L44028`, `rule.13.Parse-StringState-Managed@L44044`, `rule.13.Parse-StringState-View@L44060`, `def.TypeStringAst@L44078`, `def.StringStateSet@L44090`, `def.StringBuiltinTable@L44102`, `def.StringBuiltinSig@L44124`, `rule.13.WF-String@L44138`
- `rule.13.Sub-String-State@L44154`, `req.StringBuiltinsTyping@L44168`, `def.StringLiteralVal@L44182`, `def.StringBytesStoreDomains@L44194`, `def.ViewBytes@L44210`, `def.ByteSeqOf@L44222`, `def.ByteLen@L44237`, `def.StringValueTypes@L44249`
- `def.StringBytesJudgementSet@L44982`, `req.StringLiteralStorage@L44284`, `rule.13.StringFrom-Ok@L44298`, `rule.13.StringFrom-Err@L44314`, `rule.13.StringAsView-Ok@L44330`, `rule.13.StringToManaged-Ok@L44362`, `rule.13.StringToManaged-Err@L44378`, `rule.13.StringCloneWith-Ok@L44394`
- `rule.13.StringCloneWith-Err@L44410`, `rule.13.StringAppend-Ok@L44426`, `rule.13.StringAppend-Err@L44442`, `rule.13.StringLength@L44458`, `rule.13.StringIsEmpty@L44474`, `def.StringViewOf@L44490`, `def.StringRuntimeLength@L44504`, `def.StringManagedLoweringLayout@L44519`
- `def.StringViewLoweringLayout@L44533`, `rule.13.Size-String-Managed@L44547`, `rule.13.Align-String-Managed@L44563`, `rule.13.Layout-String-Managed@L44579`, `rule.13.Size-String-View@L44595`, `rule.13.Align-String-View@L44611`, `rule.13.Layout-String-View@L44627`, `rule.13.Size-String-Modal@L44643`
- `rule.13.Align-String-Modal@L44659`, `def.StringValueBits@L44675`, `def.DropManagedString@L44687`, `diag.StringTypes@L44701`, `grammar.BytesTypes.Syntax@L44717`, `rule.13.Parse-Bytes-Type@L44734`, `rule.13.Parse-BytesState-None@L44752`, `rule.13.Parse-BytesState-Managed@L44768`
- `rule.13.Parse-BytesState-View@L44784`, `def.TypeBytesAst@L44802`, `def.BytesStateSet@L44814`, `def.BytesBuiltinTable@L44826`, `def.StringBytesBuiltinTable@L44850`, `def.BytesBuiltinSig@L44862`, `def.StringBytesBuiltinSig@L44874`, `rule.13.WF-Bytes@L44889`
- `rule.13.Sub-Bytes-State@L44905`, `req.BytesBuiltinsTyping@L44919`, `def.SliceBytes@L44933`, `def.BytesValueTypes@L44945`, `def.BytesJudgementSet@L44959`, `def.StringBytesJudgementSet@L44982`, `rule.13.BytesWithCapacity-Ok@L44994`, `rule.13.BytesWithCapacity-Err@L45010`
- `rule.13.BytesFromSlice-Ok@L45026`, `rule.13.BytesFromSlice-Err@L45042`, `rule.13.BytesAsView-Ok@L45058`, `rule.13.BytesToManaged-Ok@L45074`, `rule.13.BytesToManaged-Err@L45090`, `rule.13.BytesView-Ok@L45106`, `rule.13.BytesViewString-Ok@L45122`, `rule.13.BytesAsSlice-Ok@L45138`
- `rule.13.BytesAppend-Ok@L45154`, `rule.13.BytesAppend-Err@L45170`, `rule.13.BytesLength@L45186`, `rule.13.BytesIsEmpty@L45202`, `def.BytesViewOf@L45218`, `def.BytesRuntimeLength@L45232`, `def.BytesViewConversions@L45245`, `def.BytesManagedLoweringLayout@L45260`
- `def.BytesViewLoweringLayout@L45274`, `rule.13.Size-Bytes-Managed@L45288`, `rule.13.Align-Bytes-Managed@L45304`, `rule.13.Layout-Bytes-Managed@L45320`, `rule.13.Size-Bytes-View@L45336`, `rule.13.Align-Bytes-View@L45352`, `rule.13.Layout-Bytes-View@L45368`, `rule.13.Size-Bytes-Modal@L45384`
- `rule.13.Align-Bytes-Modal@L45400`, `def.BytesValueBits@L45416`, `def.DropManagedBytes@L45428`, `diag.BytesTypes@L45442`, `grammar.SafePointerTypes.Syntax@L45458`, `rule.13.Parse-Safe-Pointer-Type-ShiftSplit@L45475`, `rule.13.Parse-Safe-Pointer-Type@L45491`, `rule.13.Parse-PtrState-None@L45509`
- `rule.13.Parse-PtrState-Valid@L45525`, `rule.13.Parse-PtrState-Null@L45541`, `rule.13.Parse-PtrState-Expired@L45557`, `def.PtrStateSet@L45575`, `def.SafePointerTypeForms@L45587`, `rule.13.WF-Ptr@L45602`, `def.SafePointerTraits@L45618`, `rule.13.Sub-Ptr-State@L45632`
- `def.SafePointerRuntimeConstructors@L45650`, `def.SafePointerValueType@L45664`, `def.PtrStateImmediate@L45676`, `def.PtrStateValid@L45689`, `def.PtrAddrJudgementSet@L45704`, `rule.13.ReadPtr-Safe@L45716`, `rule.13.WritePtr-Safe@L45732`, `rule.13.ReadPtr-Null@L45748`
- `rule.13.ReadPtr-Expired@L45764`, `rule.13.WritePtr-Null@L45780`, `rule.13.WritePtr-Expired@L45796`, `rule.13.Size-Ptr@L45814`, `rule.13.Align-Ptr@L45830`, `rule.13.Layout-Ptr@L45846`, `def.SafePointerSizeAlignEquations@L45862`, `def.PtrDiagRefs@L45874`
- `def.SafePointerNicheSet@L45886`, `def.SafePointerValidValue@L45899`, `def.SafePointerValueBits@L45914`, `diag.SafePointerTypes@L45931`, `grammar.RawPointerTypes.Syntax@L45947`, `rule.13.Parse-Raw-Pointer-Type@L45963`, `def.RawPointerTypes.AST@L45981`, `rule.13.WF-RawPtr@L45995`
- `rule.13.T-Deref-Raw@L46011`, `rule.13.P-Deref-Raw-Imm@L46027`, `rule.13.P-Deref-Raw-Mut@L46043`, `rule.13.Deref-Raw-Unsafe@L46059`, `def.RawPointerRuntimeValue@L46077`, `rule.13.ReadPtr-Raw@L46089`, `rule.13.WritePtr-Raw@L46105`, `rule.13.ReadPtr-Raw-Invalid@L46121`
- `rule.13.WritePtr-Raw-Imm@L46137`, `rule.13.WritePtr-Raw-Invalid@L46153`, `rule.13.Size-RawPtr@L46171`, `rule.13.Align-RawPtr@L46187`, `rule.13.Layout-RawPtr@L46203`, `def.RawPointerValidValue@L46219`, `def.RawPointerValueBits@L46231`, `req.RawPointerLowering@L46242`
- `diag.RawPointerTypes@L46256`, `grammar.FunctionTypes.Syntax@L46272`, `req.FunctionTypeTrailingComma@L46288`, `rule.13.Parse-Func-Type@L46302`, `rule.13.Parse-ParamType-Move@L46318`, `rule.13.Parse-ParamType-Plain@L46334`, `rule.13.Parse-ParamTypeList-Empty@L46350`, `rule.13.Parse-ParamTypeList-Cons@L46366`
- `rule.13.Parse-ParamTypeListTail-End@L46382`, `rule.13.Parse-ParamTypeListTail-TrailingComma@L46398`, `rule.13.Parse-ParamTypeListTail-Cons@L46414`, `def.FunctionTypes.AST@L46432`, `rule.13.WF-Func@L46446`, `rule.13.T-Equiv-Func@L46462`, `rule.13.Sub-Func@L46478`, `rule.13.T-Proc-As-Value@L46494`
- `diag.FunctionTypeCalls@L46510`, `def.FunctionRuntimeValue@L46524`, `rule.13.EvalSigma-Call-Proc@L46536`, `req.NamedProceduresFirstClass@L46552`, `rule.13.Size-Func@L46566`, `rule.13.Align-Func@L46582`, `rule.13.Layout-Func@L46598`, `req.FunctionTypeCallLowering@L46614`
- `diag.FunctionTypes@L46628`, `grammar.ClosureTypes.Syntax@L46644`, `req.ClosureParamUnionParentheses@L46661`, `rule.13.Parse-Closure-Type@L46675`, `rule.13.Parse-Closure-Type-Empty@L46691`, `rule.13.Parse-ClosureParamType-Grouped@L46707`, `rule.13.Parse-ClosureParamType-Plain@L46723`, `rule.13.Parse-ClosureParamTypeList-Empty@L46739`
- `rule.13.Parse-ClosureParamTypeList-Cons@L46755`, `rule.13.Parse-ClosureParamTypeListTail-End@L46771`, `rule.13.Parse-ClosureParamTypeListTail-TrailingComma@L46787`, `rule.13.Parse-ClosureParamTypeListTail-Comma@L46803`, `rule.13.Parse-ClosureDepsOpt-None@L46819`, `rule.13.Parse-ClosureDepsOpt-Some@L46835`, `rule.13.Parse-SharedDepList-Empty@L46851`, `rule.13.Parse-SharedDepList-Single@L46867`
- `rule.13.Parse-SharedDepList-Cons@L46883`, `rule.13.Parse-SharedDep@L46899`, `def.TypeClosureAst@L46917`, `def.ClosureDepsOpt@L46929`, `req.ClosureTypeOwnershipBoundaries@L46941`, `rule.13.WF-Closure@L46955`, `rule.13.T-Equiv-Closure@L46971`, `rule.13.Sub-Closure@L46987`
- `req.ClosureExpressionOwnership@L47003`, `def.ClosureRuntimeValue@L47017`, `req.ClosureOperationOwnership@L47029`, `def.ClosureLoweringRep@L47043`, `rule.13.Size-Closure@L47055`, `rule.13.Align-Closure@L47071`, `rule.13.Layout-Closure@L47087`, `req.ClosureLoweringOwnership@L47103`
- `diag.ClosureTypes@L47117`, `diagnostics.ModalPointerSupplement@L47129`

#### `spec.abstraction-polymorphism`

Count: 328 total; 327 required; 0 recommended; 0 informative. Ledger line span: L46800-L51908.

- `grammar.GenericParamsAndArgsSyntax@L47177`, `req.GenericArgsTrailingComma@L47195`, `req.GenericParamInlineBoundsClassOnly@L47207`, `rule.14.Parse-GenericArgs@L47221`, `rule.14.Parse-GenericArgsOpt-None@L47237`, `rule.14.Parse-GenericArgsOpt-Yes@L47253`, `rule.14.Parse-GenericParamsOpt-None@L47269`, `rule.14.Parse-GenericParamsOpt-Yes@L47285`
- `rule.14.Parse-GenericParams@L47301`, `rule.14.Parse-TypeParamTail-End@L47317`, `rule.14.Parse-TypeParamTail-Cons@L47333`, `rule.14.Parse-TypeParam@L47349`, `rule.14.Parse-TypeBoundsOpt-None@L47365`, `rule.14.Parse-TypeBoundsOpt-Yes@L47381`, `rule.14.Parse-ClassBoundList-Cons@L47397`, `rule.14.Parse-ClassBoundListTail-End@L47413`
- `rule.14.Parse-ClassBoundListTail-Cons@L47429`, `rule.14.Parse-ClassBound@L47445`, `rule.14.Parse-TypeDefaultOpt-None@L47461`, `rule.14.Parse-TypeDefaultOpt-Yes@L47477`, `rule.14.Parse-PredicateClauseOpt-None@L47493`, `rule.14.Parse-PredicateClauseOpt-Yes@L47509`, `rule.14.Parse-PredicateReqList-Cons@L47525`, `rule.14.Parse-PredicateReqListTail-End@L47541`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47557`, `rule.14.Parse-PredicateReqListTail-Cons@L47573`, `def.PredicateNameParserSet@L47589`, `rule.14.Parse-PredicateReq-Predicate@L47601`, `rule.14.Parse-PredicateReq-Err@L47617`, `def.VarianceSet@L47635`, `def.GenericParamAst@L47647`, `def.PredicateClauseAst@L47661`
- `def.GenericParamHelpers@L47675`, `def.GenericDefaultWellFormedness@L47694`, `rule.14.WF-Generic-Param@L47708`, `def.DefaultArgs@L47724`, `rule.14.PredicateReq-WF-Predicate@L47741`, `def.PredicateClauseWellFormedness@L47757`, `def.PredOk@L47769`, `rule.14.T-Constraint-Sat@L47784`
- `rule.14.PredicateReq-Predicate@L47800`, `def.PredicateClauseSubstitutionOk@L47816`, `req.GenericBoundsAndPredicatesConjunctive@L47828`, `conformance.GenericParamsNoRuntimeSemantics@L47842`, `conformance.GenericParamsLoweringInputsOnly@L47856`, `diag.GenericParametersAndArguments@L47870`, `grammar.GenericProceduresAndTypesSyntax@L47886`, `req.GenericParamsNominalOwnerChapters@L47902`
- `req.GenericDeclarationParsingDelegated@L47916`, `def.CallTypeArgsStart@L47928`, `rule.14.Postfix-Call-TypeArgs@L47940`, `def.GenericDeclarationAstExtensions@L47958`, `def.GenericApplyAst@L47975`, `def.GenericDeclarationAccessors@L47989`, `rule.14.WF-Generic-Proc@L48004`, `def.GenericCalleeProc@L48020`
- `def.GenericInferenceFreshArgs@L48034`, `def.InferTypeArgs@L48047`, `rule.14.GenericCallInference@L48065`, `rule.14.T-Generic-Call@L48096`, `rule.14.Generic-Call-ArgCount-Err@L48119`, `rule.14.WF-Path-Generic-Err@L48135`, `rule.14.WF-Apply@L48151`, `rule.14.WF-Apply-ArgCount-Err@L48167`
- `req.GenericCallInferenceElaboration@L48183`, `conformance.GenericInstantiationDynamicElaboration@L48197`, `conformance.GenericMonomorphicInstantiationsDistinct@L48209`, `def.MonomorphizationSpecialization@L48223`, `req.GenericProcedureCallLowering@L48237`, `req.GenericInstantiationIndependentLowering@L48249`, `req.GenericInfiniteMonomorphizationRejected@L48261`, `req.GenericInstantiationDepthLimit@L48273`
- `req.GenericNominalSizeAlignSubstitutedBody@L48285`, `diag.GenericProceduresAndTypes@L48299`, `grammar.ClassesSyntax@L48315`, `req.AssociatedTypeSyntaxCanonicalOwner@L48332`, `rule.14.Parse-Class@L48346`, `rule.14.Parse-Superclass-None@L48362`, `rule.14.Parse-Superclass-Yes@L48378`, `rule.14.Parse-SuperclassBounds-Cons@L48394`
- `rule.14.Parse-SuperclassBoundsTail-End@L48410`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48426`, `rule.14.Parse-ClassBody@L48442`, `rule.14.Parse-ClassItemList-End@L48458`, `rule.14.Parse-ClassItemList-Cons@L48474`, `rule.14.Parse-ClassItem-Method@L48490`, `rule.14.Parse-ClassItem-Field@L48506`, `rule.14.Parse-ClassItem-AbstractState@L48522`
- `rule.14.Parse-ClassMethodBody-Concrete@L48538`, `rule.14.Parse-ClassMethodBody-Abstract@L48554`, `def.ClassDeclAst@L48572`, `def.ClassItemAst@L48585`, `def.ClassMethodAbstractConcretePredicates@L48602`, `def.ClassMemberCollections@L48615`, `def.ClassMethodReturnType@L48631`, `def.SelfVar@L48644`
- `def.DistinctDisjoint@L48658`, `rule.14.WF-ClassPath@L48671`, `rule.14.WF-ClassPath-Err@L48687`, `def.SubstSelf@L48703`, `def.ReceiverTypeHelpers@L48729`, `def.Supers@L48758`, `rule.14.T-Superclass@L48770`, `def.ClassLinearizationMergeHelpers@L48786`
- `rule.14.Lin-Base@L48806`, `rule.14.Merge-Empty@L48822`, `rule.14.Merge-Step@L48838`, `rule.14.Merge-Fail@L48854`, `rule.14.Lin-Ok@L48870`, `rule.14.Lin-Fail@L48886`, `rule.14.Superclass-Cycle@L48902`, `def.LinearizeHeadInvariant@L48918`
- `def.EffectiveClassMembers@L48930`, `def.FirstByName@L48943`, `rule.14.EffMethods-Conflict@L48960`, `def.FieldSig@L48976`, `def.FirstFieldByName@L48988`, `rule.14.EffFields-Conflict@L49005`, `def.SelfTypeClass@L49021`, `rule.14.WF-Class-Method@L49033`
- `rule.14.T-Class-Method-Body-Abstract@L49049`, `rule.14.T-Class-Method-Body@L49065`, `rule.14.WF-Class@L49081`, `conformance.ClassDeclarationsNoRuntimeActions@L49099`, `req.ClassMethodLowering@L49113`, `req.ClassDefaultMethodAndVtableOwnership@L49125`, `diag.Classes@L49139`, `grammar.ImplementationsSyntax@L49155`
- `req.NoStandaloneImplementationBlocks@L49170`, `rule.14.Parse-Implements-None@L49184`, `rule.14.Parse-Implements-Yes@L49200`, `rule.14.Parse-ClassList-Cons@L49216`, `rule.14.Parse-ClassListTail-End@L49232`, `rule.14.Parse-ClassListTail-Comma@L49248`, `def.ImplementsAccessor@L49266`, `req.SubtypeOperatorImplementationMeaning@L49278`
- `req.ImplementationsConcreteOwnerOnly@L49293`, `def.ImplementerFields@L49305`, `def.ImplementerMethods@L49318`, `def.MethodByName@L49331`, `def.ClassEffectiveTables@L49344`, `def.ImplementationOrphanRule@L49357`, `def.DefaultMethodPredicates@L49373`, `rule.14.Impl-Abstract-Method@L49386`
- `rule.14.Impl-Missing-Method@L49402`, `rule.14.Impl-AssocType-Missing@L49418`, `rule.14.Impl-Sig-Err@L49434`, `rule.14.Override-Abstract-Err@L49450`, `rule.14.Impl-Concrete-Default@L49466`, `rule.14.Impl-Concrete-Override@L49482`, `rule.14.Override-Missing-Err@L49498`, `rule.14.Impl-Sig-Err-Concrete@L49514`
- `rule.14.Override-NoConcrete@L49530`, `rule.14.Impl-Field@L49546`, `rule.14.Impl-Field-Missing@L49562`, `rule.14.Impl-Field-Type-Err@L49578`, `rule.14.Impl-Coherence-Err@L49594`, `rule.14.Impl-Orphan-Err@L49610`, `rule.14.WF-Impl@L49626`, `rule.14.ImplementationSubtypeRelation@L49642`
- `req.14.ModalClassImplementationRequiresModalType@L49655`, `req.14.DuplicateClassImplementationForbidden@L49668`, `req.14.ImplementationOrphanRequirement@L49681`, `req.14.ImplementationsNoAdditionalRuntimeState@L49696`, `req.14.ImplementationBodyLowering@L49711`, `diag.14.Implementations@L49726`, `grammar.14.AssociatedType@L49743`, `req.14.AssociatedTypeEqualsMeaning@L49758`
- `rule.14.Parse-ClassItem-AssociatedType@L49773`, `rule.14.Parse-AssocTypeOpt-None@L49789`, `rule.14.Parse-AssocTypeOpt-Yes@L49805`, `rule.14.Parse-AssocTypeDefaultOpt@L49821`, `rule.14.Parse-RecordMember-AssociatedType@L49837`, `def.14.AssociatedTypeDeclAst@L49855`, `def.14.AssociatedTypeAstMembership@L49868`, `def.14.AssociatedTypeClassAbstractDefaulted@L49882`
- `def.14.AssocTypeItemsAndNames@L49895`, `def.14.AssocTypeDefault@L49909`, `def.14.ImplAssocType@L49923`, `def.14.AbstractAssociatedTypeNames@L49937`, `def.14.AssocTypeBinding@L49950`, `def.14.AssocTypeBindsPredicate@L49965`, `req.14.GenericParametersAssociatedTypesSupplySites@L49980`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49993`
- `req.14.ImplementationAssociatedTypeBoundForm@L50006`, `def.14.AssociatedTypeLookupOrder@L50019`, `rule.14.T-Alias-Equiv@L50036`, `req.14.AssociatedTypesNoRuntimeSemantics@L50054`, `req.14.AssociatedTypeErasureLowering@L50069`, `diag.14.AssociatedTypes@L50084`, `grammar.14.DynamicClassObjects@L50101`, `req.14.DynamicMethodCallSurfaceSyntax@L50117`
- `rule.14.Parse-Dynamic-Type@L50132`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50148`, `def.14.TypeDynamicAst@L50163`, `def.14.DynamicClassLayoutFields@L50176`, `def.14.DynamicClassRuntimeValue@L50190`, `def.14.SelfOccurs@L50203`, `def.14.DynamicDispatchEligibility@L50230`, `rule.14.WF-Dynamic@L50248`
- `rule.14.WF-Dynamic-Err@L50264`, `rule.14.T-Equiv-Dynamic@L50280`, `rule.14.T-Dynamic-Form@L50296`, `rule.14.Dynamic-NonDispatchable@L50312`, `def.14.LookupMethod@L50328`, `rule.14.T-Dynamic-MethodCall@L50343`, `rule.14.LookupClassMethod-NotFound@L50359`, `req.14.DynamicDispatchDispatchableClassesOnly@L50375`
- `def.14.DynamicValueType@L50390`, `rule.14.Eval-Dynamic-Form@L50403`, `rule.14.Eval-Dynamic-Form-Ctrl@L50419`, `def.14.DynamicDispatchSelection@L50435`, `def.14.DynamicMethodTarget@L50450`, `rule.14.Layout-DynamicClass@L50465`, `rule.14.Size-DynamicClass@L50480`, `rule.14.Align-DynamicClass@L50496`
- `rule.14.ABI-Dynamic@L50512`, `def.14.DynamicValueBits@L50528`, `def.14.DynamicDispatchLoweringJudgements@L50541`, `rule.14.DispatchSym-Impl@L50555`, `rule.14.DispatchSym-Default-None@L50571`, `rule.14.DispatchSym-Default-Mismatch@L50587`, `rule.14.VTable-Order@L50603`, `rule.14.VSlot-Entry@L50619`
- `rule.14.Lower-Dynamic-Form@L50635`, `rule.14.Lower-DynCall@L50651`, `rule.14.EmitVTable-Decl@L50667`, `diag.14.DynamicClassObjects@L50685`, `grammar.14.OpaqueTypes@L50702`, `req.14.OpaqueTypesComposeAsTypeForms@L50717`, `rule.14.Parse-Opaque-Type@L50732`, `def.14.TypeOpaqueAst@L50750`
- `def.14.TypeOpaqueForm@L50763`, `rule.14.WF-Opaque@L50778`, `rule.14.WF-Opaque-Err@L50794`, `rule.14.T-Equiv-Opaque@L50810`, `rule.14.T-Opaque-Return@L50826`, `rule.14.T-Opaque-Project@L50842`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50858`, `req.14.OpaqueTypesNoRuntimeWrapper@L50873`
- `req.14.OpaqueTypesLowerAsConcrete@L50888`, `diag.14.OpaqueTypes@L50903`, `grammar.14.RefinementTypes@L50920`, `req.14.RefinementSelfBinding@L50937`, `rule.14.Parse-RefinementOpt-None@L50952`, `rule.14.Parse-RefinementOpt-Yes@L50968`, `rule.14.ParsePredicateExpr@L50984`, `def.14.TypeRefineAst@L50999`
- `def.14.TypeRefineForm@L51012`, `def.14.PredicateEquiv@L51025`, `rule.14.T-Equiv-Refine@L51040`, `rule.14.T-Equiv-Refine-Norm@L51056`, `rule.14.WF-Refine-Type@L51072`, `rule.14.T-Refine-Intro@L51088`, `rule.14.T-Refine-Elim@L51104`, `rule.14.RefinementSubtypeBase@L51120`
- `rule.14.RefinementSubtypeImplication@L51135`, `req.14.RefinementDecidablePredicateFragment@L51150`, `req.14.RefinementStaticDefaultDynamicFallback@L51163`, `req.14.RefinementRuntimeRepresentationAndPanic@L51178`, `rule.14.LLVMTy-Refine@L51193`, `req.14.RefinementRuntimeCheckLowering@L51209`, `diag.14.RefinementTypes@L51224`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51241`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51256`, `def.14.CapClassSet@L51271`, `def.14.CapType@L51284`, `def.14.FileSystemInterface@L51297`, `def.14.NetworkInterface@L51328`, `def.14.HeapAllocatorInterface@L51344`, `def.14.FileKindDecl@L51362`, `def.14.IoErrorDecl@L51380`
- `def.14.DirEntryDecl@L51401`, `def.14.AllocationErrorDecl@L51419`, `def.14.ContextDecl@L51436`, `def.14.SystemDecl@L51462`, `def.14.ExecutionDomainSupportDecls@L51494`, `def.14.ReactorDecl@L51513`, `def.14.CapMethodSig@L51533`, `def.14.CapRecv@L51550`
- `def.14.CapabilityLoweringSupport@L51566`, `req.14.CapabilityClassesOrdinaryClasses@L51583`, `req.14.CapabilityClassesGenericBounds@L51596`, `req.14.CapabilityClassNamesReserved@L51609`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51622`, `rule.14.AllocRaw-Unsafe-Err@L51635`, `rule.14.DeallocRaw-Unsafe-Err@L51651`, `def.14.BuiltinTypesFS@L51667`
- `def.14.BuiltinDeclLookup@L51680`, `def.14.BuiltinTypeEnvironment@L51699`, `def.14.BuiltInContext@L51719`, `def.14.ContextBundleFieldType@L51732`, `def.14.ContextBundleType@L51752`, `def.14.ContextBundleFieldValue@L51766`, `def.14.ContextDomainValue@L51786`, `def.14.ContextBundleBuild@L51799`
- `def.14.AllocErrorVal@L51815`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51830`, `req.14.CapabilityBuiltinMethodLowering@L51845`, `diag.14.CapabilityClasses@L51860`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51877`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51892`, `def.14.FoundationalClassName@L51907`, `def.14.FoundationalJudgements@L51920`
- `def.14.HasCloneDropMethod@L51936`, `def.14.CloneDropTypePredicates@L51950`, `def.14.FoundationalImplementationPredicates@L51964`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51984`, `rule.14.BitcopyDrop-Ok@L51997`, `rule.14.BitcopyDrop-Conflict@L52013`, `def.14.BitcopyType@L52029`, `def.14.BitcopyTypeCore@L52042`
- `def.14.BuiltinBitcopyType@L52065`, `def.14.BuiltinDropCloneType@L52096`, `def.14.BuiltinFoundationalClassSignatures@L52110`, `req.14.EqLaws@L52129`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52142`, `req.14.IteratorNextContract@L52155`, `req.14.StepPartialInverseContract@L52168`, `req.14.DropCloneDynamicSemantics@L52183`
- `req.14.HasherDynamicSemantics@L52196`, `req.14.IntegerStepDynamicSemantics@L52209`, `req.14.CharStepDynamicSemantics@L52222`, `req.14.FoundationalIntrinsicCallLowering@L52237`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52250`, `diag.14.FoundationalClasses@L52265`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52280`, `diag-table.14.RefinementPolymorphismDiagnostics@L52293`
- `grammar.GenericParamsAndArgsSyntax@L47177`, `req.GenericArgsTrailingComma@L47195`, `req.GenericParamInlineBoundsClassOnly@L47207`, `rule.14.Parse-GenericArgs@L47221`, `rule.14.Parse-GenericArgsOpt-None@L47237`, `rule.14.Parse-GenericArgsOpt-Yes@L47253`, `rule.14.Parse-GenericParamsOpt-None@L47269`, `rule.14.Parse-GenericParamsOpt-Yes@L47285`
- `rule.14.Parse-GenericParams@L47301`, `rule.14.Parse-TypeParamTail-End@L47317`, `rule.14.Parse-TypeParamTail-Cons@L47333`, `rule.14.Parse-TypeParam@L47349`, `rule.14.Parse-TypeBoundsOpt-None@L47365`, `rule.14.Parse-TypeBoundsOpt-Yes@L47381`, `rule.14.Parse-ClassBoundList-Cons@L47397`, `rule.14.Parse-ClassBoundListTail-End@L47413`
- `rule.14.Parse-ClassBoundListTail-Cons@L47429`, `rule.14.Parse-ClassBound@L47445`, `rule.14.Parse-TypeDefaultOpt-None@L47461`, `rule.14.Parse-TypeDefaultOpt-Yes@L47477`, `rule.14.Parse-PredicateClauseOpt-None@L47493`, `rule.14.Parse-PredicateClauseOpt-Yes@L47509`, `rule.14.Parse-PredicateReqList-Cons@L47525`, `rule.14.Parse-PredicateReqListTail-End@L47541`
- `rule.14.Parse-PredicateReqListTail-TrailingTerminator@L47557`, `rule.14.Parse-PredicateReqListTail-Cons@L47573`, `def.PredicateNameParserSet@L47589`, `rule.14.Parse-PredicateReq-Predicate@L47601`, `rule.14.Parse-PredicateReq-Err@L47617`, `def.VarianceSet@L47635`, `def.GenericParamAst@L47647`, `def.PredicateClauseAst@L47661`
- `def.GenericParamHelpers@L47675`, `def.GenericDefaultWellFormedness@L47694`, `rule.14.WF-Generic-Param@L47708`, `def.DefaultArgs@L47724`, `rule.14.PredicateReq-WF-Predicate@L47741`, `def.PredicateClauseWellFormedness@L47757`, `def.PredOk@L47769`, `rule.14.T-Constraint-Sat@L47784`
- `rule.14.PredicateReq-Predicate@L47800`, `def.PredicateClauseSubstitutionOk@L47816`, `req.GenericBoundsAndPredicatesConjunctive@L47828`, `conformance.GenericParamsNoRuntimeSemantics@L47842`, `conformance.GenericParamsLoweringInputsOnly@L47856`, `diag.GenericParametersAndArguments@L47870`, `grammar.GenericProceduresAndTypesSyntax@L47886`, `req.GenericParamsNominalOwnerChapters@L47902`
- `req.GenericDeclarationParsingDelegated@L47916`, `def.CallTypeArgsStart@L47928`, `rule.14.Postfix-Call-TypeArgs@L47940`, `def.GenericDeclarationAstExtensions@L47958`, `def.GenericApplyAst@L47975`, `def.GenericDeclarationAccessors@L47989`, `rule.14.WF-Generic-Proc@L48004`, `def.GenericCalleeProc@L48020`
- `def.GenericInferenceFreshArgs@L48034`, `def.InferTypeArgs@L48047`, `rule.14.GenericCallInference@L48065`, `rule.14.T-Generic-Call@L48096`, `rule.14.Generic-Call-ArgCount-Err@L48119`, `rule.14.WF-Path-Generic-Err@L48135`, `rule.14.WF-Apply@L48151`, `rule.14.WF-Apply-ArgCount-Err@L48167`
- `req.GenericCallInferenceElaboration@L48183`, `conformance.GenericInstantiationDynamicElaboration@L48197`, `conformance.GenericMonomorphicInstantiationsDistinct@L48209`, `def.MonomorphizationSpecialization@L48223`, `req.GenericProcedureCallLowering@L48237`, `req.GenericInstantiationIndependentLowering@L48249`, `req.GenericInfiniteMonomorphizationRejected@L48261`, `req.GenericInstantiationDepthLimit@L48273`
- `req.GenericNominalSizeAlignSubstitutedBody@L48285`, `diag.GenericProceduresAndTypes@L48299`, `grammar.ClassesSyntax@L48315`, `req.AssociatedTypeSyntaxCanonicalOwner@L48332`, `rule.14.Parse-Class@L48346`, `rule.14.Parse-Superclass-None@L48362`, `rule.14.Parse-Superclass-Yes@L48378`, `rule.14.Parse-SuperclassBounds-Cons@L48394`
- `rule.14.Parse-SuperclassBoundsTail-End@L48410`, `rule.14.Parse-SuperclassBoundsTail-Plus@L48426`, `rule.14.Parse-ClassBody@L48442`, `rule.14.Parse-ClassItemList-End@L48458`, `rule.14.Parse-ClassItemList-Cons@L48474`, `rule.14.Parse-ClassItem-Method@L48490`, `rule.14.Parse-ClassItem-Field@L48506`, `rule.14.Parse-ClassItem-AbstractState@L48522`
- `rule.14.Parse-ClassMethodBody-Concrete@L48538`, `rule.14.Parse-ClassMethodBody-Abstract@L48554`, `def.ClassDeclAst@L48572`, `def.ClassItemAst@L48585`, `def.ClassMethodAbstractConcretePredicates@L48602`, `def.ClassMemberCollections@L48615`, `def.ClassMethodReturnType@L48631`, `def.SelfVar@L48644`
- `def.DistinctDisjoint@L48658`, `rule.14.WF-ClassPath@L48671`, `rule.14.WF-ClassPath-Err@L48687`, `def.SubstSelf@L48703`, `def.ReceiverTypeHelpers@L48729`, `def.Supers@L48758`, `rule.14.T-Superclass@L48770`, `def.ClassLinearizationMergeHelpers@L48786`
- `rule.14.Lin-Base@L48806`, `rule.14.Merge-Empty@L48822`, `rule.14.Merge-Step@L48838`, `rule.14.Merge-Fail@L48854`, `rule.14.Lin-Ok@L48870`, `rule.14.Lin-Fail@L48886`, `rule.14.Superclass-Cycle@L48902`, `def.LinearizeHeadInvariant@L48918`
- `def.EffectiveClassMembers@L48930`, `def.FirstByName@L48943`, `rule.14.EffMethods-Conflict@L48960`, `def.FieldSig@L48976`, `def.FirstFieldByName@L48988`, `rule.14.EffFields-Conflict@L49005`, `def.SelfTypeClass@L49021`, `rule.14.WF-Class-Method@L49033`
- `rule.14.T-Class-Method-Body-Abstract@L49049`, `rule.14.T-Class-Method-Body@L49065`, `rule.14.WF-Class@L49081`, `conformance.ClassDeclarationsNoRuntimeActions@L49099`, `req.ClassMethodLowering@L49113`, `req.ClassDefaultMethodAndVtableOwnership@L49125`, `diag.Classes@L49139`, `grammar.ImplementationsSyntax@L49155`
- `req.NoStandaloneImplementationBlocks@L49170`, `rule.14.Parse-Implements-None@L49184`, `rule.14.Parse-Implements-Yes@L49200`, `rule.14.Parse-ClassList-Cons@L49216`, `rule.14.Parse-ClassListTail-End@L49232`, `rule.14.Parse-ClassListTail-Comma@L49248`, `def.ImplementsAccessor@L49266`, `req.SubtypeOperatorImplementationMeaning@L49278`
- `req.ImplementationsConcreteOwnerOnly@L49293`, `def.ImplementerFields@L49305`, `def.ImplementerMethods@L49318`, `def.MethodByName@L49331`, `def.ClassEffectiveTables@L49344`, `def.ImplementationOrphanRule@L49357`, `def.DefaultMethodPredicates@L49373`, `rule.14.Impl-Abstract-Method@L49386`
- `rule.14.Impl-Missing-Method@L49402`, `rule.14.Impl-AssocType-Missing@L49418`, `rule.14.Impl-Sig-Err@L49434`, `rule.14.Override-Abstract-Err@L49450`, `rule.14.Impl-Concrete-Default@L49466`, `rule.14.Impl-Concrete-Override@L49482`, `rule.14.Override-Missing-Err@L49498`, `rule.14.Impl-Sig-Err-Concrete@L49514`
- `rule.14.Override-NoConcrete@L49530`, `rule.14.Impl-Field@L49546`, `rule.14.Impl-Field-Missing@L49562`, `rule.14.Impl-Field-Type-Err@L49578`, `rule.14.Impl-Coherence-Err@L49594`, `rule.14.Impl-Orphan-Err@L49610`, `rule.14.WF-Impl@L49626`, `rule.14.ImplementationSubtypeRelation@L49642`
- `req.14.ModalClassImplementationRequiresModalType@L49655`, `req.14.DuplicateClassImplementationForbidden@L49668`, `req.14.ImplementationOrphanRequirement@L49681`, `req.14.ImplementationsNoAdditionalRuntimeState@L49696`, `req.14.ImplementationBodyLowering@L49711`, `diag.14.Implementations@L49726`, `grammar.14.AssociatedType@L49743`, `req.14.AssociatedTypeEqualsMeaning@L49758`
- `rule.14.Parse-ClassItem-AssociatedType@L49773`, `rule.14.Parse-AssocTypeOpt-None@L49789`, `rule.14.Parse-AssocTypeOpt-Yes@L49805`, `rule.14.Parse-AssocTypeDefaultOpt@L49821`, `rule.14.Parse-RecordMember-AssociatedType@L49837`, `def.14.AssociatedTypeDeclAst@L49855`, `def.14.AssociatedTypeAstMembership@L49868`, `def.14.AssociatedTypeClassAbstractDefaulted@L49882`
- `def.14.AssocTypeItemsAndNames@L49895`, `def.14.AssocTypeDefault@L49909`, `def.14.ImplAssocType@L49923`, `def.14.AbstractAssociatedTypeNames@L49937`, `def.14.AssocTypeBinding@L49950`, `def.14.AssocTypeBindsPredicate@L49965`, `req.14.GenericParametersAssociatedTypesSupplySites@L49980`, `req.14.AssociatedTypeAbstractAndDefaultBinding@L49993`
- `req.14.ImplementationAssociatedTypeBoundForm@L50006`, `def.14.AssociatedTypeLookupOrder@L50019`, `rule.14.T-Alias-Equiv@L50036`, `req.14.AssociatedTypesNoRuntimeSemantics@L50054`, `req.14.AssociatedTypeErasureLowering@L50069`, `diag.14.AssociatedTypes@L50084`, `grammar.14.DynamicClassObjects@L50101`, `req.14.DynamicMethodCallSurfaceSyntax@L50117`
- `rule.14.Parse-Dynamic-Type@L50132`, `req.14.DynamicCastUsesOrdinaryCastParsing@L50148`, `def.14.TypeDynamicAst@L50163`, `def.14.DynamicClassLayoutFields@L50176`, `def.14.DynamicClassRuntimeValue@L50190`, `def.14.SelfOccurs@L50203`, `def.14.DynamicDispatchEligibility@L50230`, `rule.14.WF-Dynamic@L50248`
- `rule.14.WF-Dynamic-Err@L50264`, `rule.14.T-Equiv-Dynamic@L50280`, `rule.14.T-Dynamic-Form@L50296`, `rule.14.Dynamic-NonDispatchable@L50312`, `def.14.LookupMethod@L50328`, `rule.14.T-Dynamic-MethodCall@L50343`, `rule.14.LookupClassMethod-NotFound@L50359`, `req.14.DynamicDispatchDispatchableClassesOnly@L50375`
- `def.14.DynamicValueType@L50390`, `rule.14.Eval-Dynamic-Form@L50403`, `rule.14.Eval-Dynamic-Form-Ctrl@L50419`, `def.14.DynamicDispatchSelection@L50435`, `def.14.DynamicMethodTarget@L50450`, `rule.14.Layout-DynamicClass@L50465`, `rule.14.Size-DynamicClass@L50480`, `rule.14.Align-DynamicClass@L50496`
- `rule.14.ABI-Dynamic@L50512`, `def.14.DynamicValueBits@L50528`, `def.14.DynamicDispatchLoweringJudgements@L50541`, `rule.14.DispatchSym-Impl@L50555`, `rule.14.DispatchSym-Default-None@L50571`, `rule.14.DispatchSym-Default-Mismatch@L50587`, `rule.14.VTable-Order@L50603`, `rule.14.VSlot-Entry@L50619`
- `rule.14.Lower-Dynamic-Form@L50635`, `rule.14.Lower-DynCall@L50651`, `rule.14.EmitVTable-Decl@L50667`, `diag.14.DynamicClassObjects@L50685`, `grammar.14.OpaqueTypes@L50702`, `req.14.OpaqueTypesComposeAsTypeForms@L50717`, `rule.14.Parse-Opaque-Type@L50732`, `def.14.TypeOpaqueAst@L50750`
- `def.14.TypeOpaqueForm@L50763`, `rule.14.WF-Opaque@L50778`, `rule.14.WF-Opaque-Err@L50794`, `rule.14.T-Equiv-Opaque@L50810`, `rule.14.T-Opaque-Return@L50826`, `rule.14.T-Opaque-Project@L50842`, `req.14.OpaqueEquivalenceAndInterfaceExposure@L50858`, `req.14.OpaqueTypesNoRuntimeWrapper@L50873`
- `req.14.OpaqueTypesLowerAsConcrete@L50888`, `diag.14.OpaqueTypes@L50903`, `grammar.14.RefinementTypes@L50920`, `req.14.RefinementSelfBinding@L50937`, `rule.14.Parse-RefinementOpt-None@L50952`, `rule.14.Parse-RefinementOpt-Yes@L50968`, `rule.14.ParsePredicateExpr@L50984`, `def.14.TypeRefineAst@L50999`
- `def.14.TypeRefineForm@L51012`, `def.14.PredicateEquiv@L51025`, `rule.14.T-Equiv-Refine@L51040`, `rule.14.T-Equiv-Refine-Norm@L51056`, `rule.14.WF-Refine-Type@L51072`, `rule.14.T-Refine-Intro@L51088`, `rule.14.T-Refine-Elim@L51104`, `rule.14.RefinementSubtypeBase@L51120`
- `rule.14.RefinementSubtypeImplication@L51135`, `req.14.RefinementDecidablePredicateFragment@L51150`, `req.14.RefinementStaticDefaultDynamicFallback@L51163`, `req.14.RefinementRuntimeRepresentationAndPanic@L51178`, `rule.14.LLVMTy-Refine@L51193`, `req.14.RefinementRuntimeCheckLowering@L51209`, `diag.14.RefinementTypes@L51224`, `req.14.CapabilityClassSyntaxUsesOrdinaryClassAndDynamicSyntax@L51241`
- `req.14.CapabilityClassNoFeatureSpecificParser@L51256`, `def.14.CapClassSet@L51271`, `def.14.CapType@L51284`, `def.14.FileSystemInterface@L51297`, `def.14.NetworkInterface@L51328`, `def.14.HeapAllocatorInterface@L51344`, `def.14.FileKindDecl@L51362`, `def.14.IoErrorDecl@L51380`
- `def.14.DirEntryDecl@L51401`, `def.14.AllocationErrorDecl@L51419`, `def.14.ContextDecl@L51436`, `def.14.SystemDecl@L51462`, `def.14.ExecutionDomainSupportDecls@L51494`, `def.14.ReactorDecl@L51513`, `def.14.CapMethodSig@L51533`, `def.14.CapRecv@L51550`
- `def.14.CapabilityLoweringSupport@L51566`, `req.14.CapabilityClassesOrdinaryClasses@L51583`, `req.14.CapabilityClassesGenericBounds@L51596`, `req.14.CapabilityClassNamesReserved@L51609`, `req.14.HeapAllocatorRawCallsRequireUnsafe@L51622`, `rule.14.AllocRaw-Unsafe-Err@L51635`, `rule.14.DeallocRaw-Unsafe-Err@L51651`, `def.14.BuiltinTypesFS@L51667`
- `def.14.BuiltinDeclLookup@L51680`, `def.14.BuiltinTypeEnvironment@L51699`, `def.14.BuiltInContext@L51719`, `def.14.ContextBundleFieldType@L51732`, `def.14.ContextBundleType@L51752`, `def.14.ContextBundleFieldValue@L51766`, `def.14.ContextDomainValue@L51786`, `def.14.ContextBundleBuild@L51799`
- `def.14.AllocErrorVal@L51815`, `req.14.CapabilityClassesUseDynamicDispatchModel@L51830`, `req.14.CapabilityBuiltinMethodLowering@L51845`, `diag.14.CapabilityClasses@L51860`, `req.14.FoundationalClassesSyntaxAndReservedNames@L51877`, `req.14.FoundationalClassesNoFeatureSpecificParser@L51892`, `def.14.FoundationalClassName@L51907`, `def.14.FoundationalJudgements@L51920`
- `def.14.HasCloneDropMethod@L51936`, `def.14.CloneDropTypePredicates@L51950`, `def.14.FoundationalImplementationPredicates@L51964`, `req.14.FoundationalBoundsIntrinsicSatisfaction@L51984`, `rule.14.BitcopyDrop-Ok@L51997`, `rule.14.BitcopyDrop-Conflict@L52013`, `def.14.BitcopyType@L52029`, `def.14.BitcopyTypeCore@L52042`
- `def.14.BuiltinBitcopyType@L52065`, `def.14.BuiltinDropCloneType@L52096`, `def.14.BuiltinFoundationalClassSignatures@L52110`, `req.14.EqLaws@L52129`, `req.14.HashRequiresEqAndEqualValuesHashEqual@L52142`, `req.14.IteratorNextContract@L52155`, `req.14.StepPartialInverseContract@L52168`, `req.14.DropCloneDynamicSemantics@L52183`
- `req.14.HasherDynamicSemantics@L52196`, `req.14.IntegerStepDynamicSemantics@L52209`, `req.14.CharStepDynamicSemantics@L52222`, `req.14.FoundationalIntrinsicCallLowering@L52237`, `req.14.FoundationalPredicatesNoSeparateRepresentation@L52250`, `diag.14.FoundationalClasses@L52265`, `diag.14.RefinementPolymorphismDiagnosticsOwnership@L52280`, `diag-table.14.RefinementPolymorphismDiagnostics@L52293`

#### `spec.procedures-contracts`

Count: 283 total; 283 required; 0 recommended; 0 informative. Ledger line span: L51972-L56430.

- `grammar.15.ProcedureDeclarations@L52357`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52375`, `rule.15.Parse-Procedure@L52390`, `rule.15.Parse-Signature@L52406`, `rule.15.Parse-ParamList-Empty@L52422`, `rule.15.Parse-ParamList-Cons@L52438`, `rule.15.Parse-Param@L52454`, `rule.15.Parse-ParamMode-None@L52470`
- `rule.15.Parse-ParamMode-Move@L52486`, `rule.15.Parse-ParamTail-End@L52502`, `rule.15.Parse-ParamTail-TrailingComma@L52518`, `rule.15.Parse-ParamTail-Comma@L52534`, `rule.15.Parse-ReturnOpt-None@L52550`, `rule.15.Parse-ReturnOpt-Arrow@L52566`, `def.15.ProcedureDeclAst@L52584`, `def.15.ParamAst@L52597`
- `def.15.ParamNamesAndBinds@L52610`, `def.15.ProcReturn@L52624`, `def.15.BodyReturnType@L52639`, `def.15.ExplicitReturn@L52654`, `def.15.ReturnAnnOk@L52669`, `rule.15.WF-ProcedureDecl@L52682`, `def.15.DeclTyping@L52698`, `def.15.ProvBindCheck@L52713`
- `def.15.DeclTypingItem@L52726`, `rule.15.ProcedureDeclOkJudgement@L52748`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52761`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52777`, `req.15.ExportedProcedureForeignCallableObligations@L52793`, `def.15.MainEntryPointDefinitions@L52806`, `rule.15.Main-Ok@L52827`, `rule.15.Main-Bypass-NonExecutable@L52843`
- `rule.15.Main-Multiple@L52859`, `rule.15.Main-Generic-Err@L52875`, `rule.15.Main-Signature-Err@L52891`, `rule.15.Main-Missing@L52907`, `def.15.MainDiagRefs@L52923`, `def.15.FuncValDefined@L52938`, `def.15.BindParams@L52951`, `def.15.ArgumentPassingJudgements@L52964`
- `def.15.CallJudgements@L52978`, `def.15.CallTargets@L52991`, `def.15.BuiltinProcedureParams@L53007`, `def.15.SynthParams@L53021`, `def.15.CalleeProc@L53034`, `def.15.CallParams@L53048`, `def.15.ReturnOut@L53064`, `rule.15.EvalArgsSigma-Empty@L53083`
- `rule.15.EvalArgsSigma-Cons-Move@L53098`, `rule.15.EvalArgsSigma-Cons-Ref@L53114`, `rule.15.EvalArgsSigma-Ctrl-Move@L53130`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53146`, `rule.15.ApplyRegionProc-NewScoped@L53162`, `rule.15.ApplyRegionProc-Alloc@L53178`, `rule.15.ApplyRegionProc-Reset@L53194`, `rule.15.ApplyRegionProc-Freeze@L53210`
- `rule.15.ApplyRegionProc-Thaw@L53226`, `rule.15.ApplyRegionProc-Free@L53242`, `rule.15.ApplyCancelProc-New@L53258`, `rule.15.ApplyProcSigma@L53274`, `rule.15.EvalSigma-Call-Proc@L53290`, `rule.15.CG-Item-Procedure@L53308`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53324`, `diag.15.ProcedureDeclarations@L53339`
- `grammar.15.MethodsAndReceivers@L53356`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53373`, `rule.15.Parse-MethodDefAfterVis@L53388`, `rule.15.Parse-Override-Yes@L53404`, `rule.15.Parse-Override-No@L53420`, `rule.15.Parse-MethodSignature@L53436`, `rule.15.Parse-StateMethodSignature-Receiver@L53452`, `rule.15.Parse-MethodParams-None@L53468`
- `rule.15.Parse-MethodParams-Comma@L53484`, `rule.15.Parse-Receiver-Short-Const@L53500`, `rule.15.Parse-Receiver-Short-Unique@L53516`, `rule.15.Parse-Receiver-Short-Shared@L53532`, `rule.15.Parse-Receiver-Explicit@L53548`, `def.15.MethodDeclAst@L53566`, `def.15.ReceiverAst@L53579`, `def.15.RecordFieldsMethodsAndSelf@L53594`
- `def.15.SelfType@L53609`, `def.15.RecvType@L53622`, `def.15.RecvMode@L53638`, `def.15.RecvPerm@L53652`, `def.15.MethodSignaturesAndParams@L53667`, `rule.15.Recv-Explicit@L53686`, `rule.15.Record-Method-RecvSelf-Err@L53702`, `rule.15.Recv-Const@L53718`
- `rule.15.Recv-Unique@L53733`, `rule.15.Recv-Shared@L53748`, `rule.15.WF-Record-Method@L53763`, `rule.15.T-Record-Method-Body@L53779`, `rule.15.WF-Record-Methods@L53795`, `rule.15.Record-Method-Dup@L53811`, `def.15.ArgsOkJudg@L53827`, `def.15.RecvBaseType@L53840`
- `rule.15.Args-Empty@L53853`, `rule.15.Args-Cons@L53868`, `rule.15.Args-Cons-Ref@L53884`, `def.15.RecvArgOk@L53900`, `rule.15.T-Record-MethodCall@L53913`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53929`, `def.15.RecvArgMode@L53944`, `def.15.MethodOf@L53958`
- `def.15.RecvBase@L53973`, `def.15.RecvParams@L53986`, `rule.15.EvalRecvSigma-Move@L54001`, `rule.15.EvalRecvSigma-Ref-Dyn@L54017`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L54033`, `rule.15.EvalRecvSigma-Ref@L54049`, `rule.15.EvalRecvSigma-Ctrl-Move@L54065`, `rule.15.EvalRecvSigma-Ctrl-Ref@L54081`
- `def.15.BindMethodParams@L54097`, `rule.15.ApplyMethodSigma-Prim@L54110`, `rule.15.ApplyMethodSigma@L54126`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54144`, `rule.15.Mangle-Record-Method@L54157`, `rule.15.Mangle-Class-Method@L54173`, `rule.15.Mangle-State-Method@L54189`, `diag.15.MethodsAndReceivers@L54207`
- `req.15.OverloadingNoAdditionalSyntax@L54224`, `req.15.OverloadResolutionNotParserConcern@L54239`, `def.15.ClassDefaults@L54254`, `def.15.LookupMethod@L54267`, `rule.15.LookupMethod-NotFound@L54284`, `rule.15.LookupMethod-Ambig@L54300`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54316`, `req.15.FreeCallOverloadResolutionAlgorithm@L54329`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54351`, `req.15.NoRuntimeOverloadSearch@L54366`, `req.15.OverloadResolutionCompleteBeforeLowering@L54381`, `diag-table.15.Overloading@L54396`, `diag.15.MethodLookupDiagnostics@L54413`, `grammar.15.ContractClauses@L54430`, `req.15.ForeignContractStartDisambiguatesContracts@L54452`, `rule.15.Parse-ContractClauseOpt-None@L54465`
- `rule.15.Parse-ContractClauseOpt-Yes@L54481`, `rule.15.Parse-ContractBody-PostOnly@L54497`, `rule.15.Parse-ContractBody-PrePost@L54513`, `rule.15.Parse-ContractBody-PreOnly@L54529`, `def.15.ContractClauseAst@L54547`, `def.15.ContractOpt@L54560`, `rule.15.WF-Contract@L54575`, `def.15.ContractPurityJudgementIntro@L54592`
- `rule.15.Pure-Literal@L54605`, `rule.15.Pure-Ident@L54621`, `rule.15.Pure-Field@L54637`, `rule.15.Pure-Tuple-Access@L54653`, `rule.15.Pure-Index@L54669`, `rule.15.Pure-Unary@L54685`, `rule.15.Pure-Binary@L54701`, `def.15.PureOps@L54717`
- `rule.15.Pure-Cast@L54730`, `rule.15.Pure-If@L54746`, `rule.15.Pure-If-Is@L54762`, `rule.15.Pure-If-Is-No-Else@L54778`, `rule.15.Pure-If-Case@L54794`, `rule.15.Pure-If-Case-No-Else@L54810`, `rule.15.Pure-Block@L54826`, `rule.15.Pure-Tuple@L54842`
- `rule.15.Pure-Array@L54858`, `rule.15.Pure-Record@L54874`, `rule.15.Pure-Call-Builtin@L54890`, `rule.15.Pure-Call-Procedure@L54906`, `rule.15.Pure-Method-Const@L54922`, `rule.15.Pure-Comptime@L54938`, `def.15.ContractPurityHelperPredicates@L54954`, `req.15.ContractNeverPureForms@L54974`
- `def.15.PreconditionEvaluationContext@L54987`, `def.15.PostconditionEvaluationContext@L55000`, `req.15.ContractClausesNoIndependentRuntimeEffect@L55015`, `req.15.ContractClauseLoweringViaVerificationResults@L55030`, `diag.15.ContractClauses@L55045`, `req.15.PreconditionSyntaxDefinition@L55062`, `req.15.PreconditionsParsedByContractBody@L55077`, `def.15.PreconditionOf@L55092`
- `def.15.PreconditionProofContext@L55109`, `rule.15.Pre-Satisfied@L55125`, `def.15.PreconditionElisionRules@L55141`, `req.15.CallerResponsibleForPrecondition@L55161`, `req.15.PreconditionRuntimeEvaluationOrder@L55176`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55191`, `diag.15.Preconditions@L55206`, `grammar.15.Postconditions@L55223`
- `rule.15.Parse-Contract-Result@L55241`, `rule.15.Parse-Contract-Entry@L55257`, `def.15.ContractIntrinsicAst@L55275`, `def.15.PostconditionOf@L55288`, `def.15.PostconditionProofContext@L55305`, `rule.15.Post-Valid@L55320`, `def.15.PostconditionElisionRules@L55336`, `req.15.ContractResultProperties@L55356`
- `rule.15.Result-Union-Type@L55373`, `rule.15.Result-Is-Predicate@L55389`, `rule.15.Result-Narrowing@L55405`, `rule.15.Propagate-Postcondition@L55421`, `rule.15.Result-Modal@L55437`, `rule.15.Result-Generic@L55453`, `rule.15.Result-Generic-Constraint@L55469`, `req.15.ContractEntryConstraints@L55485`
- `rule.15.Entry-Type@L55503`, `req.15.PostconditionResultRuntimeBinding@L55521`, `req.15.ContractEntryRuntimeCapture@L55534`, `def.15.EntryCaptureTiming@L55551`, `rule.15.EntryCapturePhase@L55571`, `def.15.EntryCaptureValue@L55588`, `req.15.PostconditionLoweringRepresentation@L55603`, `diag.15.Postconditions@L55618`
- `grammar.15.Invariants@L55635`, `rule.15.Parse-InvariantOpt-None@L55653`, `rule.15.Parse-InvariantOpt-Yes@L55669`, `rule.15.ParseLoopInvariantOpt@L55685`, `def.15.InvariantAst@L55700`, `def.15.TypeInvariantAstExtensions@L55714`, `def.15.LoopInvariantAstPreservation@L55729`, `def.15.TypeInvariantContext@L55744`
- `def.15.TypeInvariantEnforcementPoints@L55761`, `req.15.TypeInvariantsForbidPublicMutableFields@L55778`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55791`, `def.15.LoopInvariantEnforcementPoints@L55804`, `req.15.LoopInvariantExitFact@L55821`, `req.15.InvariantVerificationModeRules@L55834`, `req.15.InvariantRuntimeChecks@L55849`, `req.15.InvariantLoweringViaVerificationLogic@L55864`
- `diag.15.Invariants@L55879`, `req.15.VerificationLogicNoSurfaceSyntax@L55896`, `req.15.VerificationLogicNotParserOwned@L55911`, `def.15.ContractKind@L55926`, `def.15.VerificationFact@L55939`, `def.15.CheckState@L55952`, `def.15.ContractCheck@L55965`, `def.15.DynamicScopeAndContext@L55980`
- `rule.15.Contract-Static-OK@L56001`, `rule.15.Contract-Static-Fail@L56017`, `rule.15.Contract-Dynamic-Elide@L56033`, `rule.15.Contract-Dynamic-Check@L56049`, `req.15.MandatoryProofTechniques@L56065`, `def.15.ProofContextAt@L56085`, `def.15.DecidablePredicates@L56106`, `rule.15.Ent-True@L56126`
- `rule.15.Ent-Fact@L56142`, `rule.15.Ent-And@L56158`, `rule.15.Ent-Or-L@L56174`, `rule.15.Ent-Or-R@L56190`, `rule.15.Ent-Linear@L56206`, `def.15.LinearIntegerEntailment@L56222`, `req.15.LinearEntailmentSoundAndComplete@L56245`, `def.15.StaticProofAt@L56258`
- `def.15.NegFact@L56271`, `req.15.VerificationFactsNoRuntimeRepresentation@L56293`, `rule.15.Fact-Dominate@L56310`, `req.15.FactGeneration@L56326`, `req.15.TypeNarrowingFromFacts@L56349`, `def.15.ContractEnvironments@L56364`, `rule.15.Check-True@L56381`, `rule.15.Check-False@L56397`
- `rule.15.Check-Panic@L56413`, `rule.15.Check-Ok@L56429`, `rule.15.Check-Fail@L56445`, `req.15.DynamicChecksInjectFacts@L56461`, `def.15.RuntimeCheckInsertionPointsIntro@L56476`, `rule.15.Insert-Precondition-Check@L56489`, `rule.15.Insert-Postcondition-Check@L56505`, `rule.15.Insert-TypeInv-Construction-Check@L56521`
- `rule.15.Insert-TypeInv-PreCall-Check@L56537`, `rule.15.Insert-TypeInv-PostCall-Check@L56553`, `rule.15.Insert-LoopInv-Init-Check@L56569`, `rule.15.Insert-LoopInv-Maintenance-Check@L56585`, `rule.15.Insert-Refinement-Check@L56601`, `diag.15.VerificationLogic@L56619`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56636`, `req.15.BehavioralSubtypingNotParserOwned@L56651`
- `def.15.BehavioralSubtypingRelationship@L56666`, `req.15.BehavioralSubtypingLiskovRequirement@L56681`, `req.15.BehavioralSubtypingPreconditionRule@L56694`, `req.15.BehavioralSubtypingPostconditionRule@L56710`, `req.15.BehavioralSubtypingVerificationStrategy@L56726`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56742`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56757`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56772`
- `diag.15.BehavioralSubtyping@L56787`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56802`, `diag-table.15.ProcedureContractEntryDiagnostics@L56815`
- `grammar.15.ProcedureDeclarations@L52357`, `req.15.ExternProcedureDeclarationsOwnedByFFI@L52375`, `rule.15.Parse-Procedure@L52390`, `rule.15.Parse-Signature@L52406`, `rule.15.Parse-ParamList-Empty@L52422`, `rule.15.Parse-ParamList-Cons@L52438`, `rule.15.Parse-Param@L52454`, `rule.15.Parse-ParamMode-None@L52470`
- `rule.15.Parse-ParamMode-Move@L52486`, `rule.15.Parse-ParamTail-End@L52502`, `rule.15.Parse-ParamTail-TrailingComma@L52518`, `rule.15.Parse-ParamTail-Comma@L52534`, `rule.15.Parse-ReturnOpt-None@L52550`, `rule.15.Parse-ReturnOpt-Arrow@L52566`, `def.15.ProcedureDeclAst@L52584`, `def.15.ParamAst@L52597`
- `def.15.ParamNamesAndBinds@L52610`, `def.15.ProcReturn@L52624`, `def.15.BodyReturnType@L52639`, `def.15.ExplicitReturn@L52654`, `def.15.ReturnAnnOk@L52669`, `rule.15.WF-ProcedureDecl@L52682`, `def.15.DeclTyping@L52698`, `def.15.ProvBindCheck@L52713`
- `def.15.DeclTypingItem@L52726`, `rule.15.ProcedureDeclOkJudgement@L52748`, `rule.15.WF-ProcedureDecl-MissingReturnType@L52761`, `rule.15.WF-ProcBody-ExplicitReturn-Err@L52777`, `req.15.ExportedProcedureForeignCallableObligations@L52793`, `def.15.MainEntryPointDefinitions@L52806`, `rule.15.Main-Ok@L52827`, `rule.15.Main-Bypass-NonExecutable@L52843`
- `rule.15.Main-Multiple@L52859`, `rule.15.Main-Generic-Err@L52875`, `rule.15.Main-Signature-Err@L52891`, `rule.15.Main-Missing@L52907`, `def.15.MainDiagRefs@L52923`, `def.15.FuncValDefined@L52938`, `def.15.BindParams@L52951`, `def.15.ArgumentPassingJudgements@L52964`
- `def.15.CallJudgements@L52978`, `def.15.CallTargets@L52991`, `def.15.BuiltinProcedureParams@L53007`, `def.15.SynthParams@L53021`, `def.15.CalleeProc@L53034`, `def.15.CallParams@L53048`, `def.15.ReturnOut@L53064`, `rule.15.EvalArgsSigma-Empty@L53083`
- `rule.15.EvalArgsSigma-Cons-Move@L53098`, `rule.15.EvalArgsSigma-Cons-Ref@L53114`, `rule.15.EvalArgsSigma-Ctrl-Move@L53130`, `rule.15.EvalArgsSigma-Ctrl-Ref@L53146`, `rule.15.ApplyRegionProc-NewScoped@L53162`, `rule.15.ApplyRegionProc-Alloc@L53178`, `rule.15.ApplyRegionProc-Reset@L53194`, `rule.15.ApplyRegionProc-Freeze@L53210`
- `rule.15.ApplyRegionProc-Thaw@L53226`, `rule.15.ApplyRegionProc-Free@L53242`, `rule.15.ApplyCancelProc-New@L53258`, `rule.15.ApplyProcSigma@L53274`, `rule.15.EvalSigma-Call-Proc@L53290`, `rule.15.CG-Item-Procedure@L53308`, `req.15.MainProgramEntryHandlingOwnedByChapter24@L53324`, `diag.15.ProcedureDeclarations@L53339`
- `grammar.15.MethodsAndReceivers@L53356`, `req.15.ClassAndStateMethodsReuseReceiverForms@L53373`, `rule.15.Parse-MethodDefAfterVis@L53388`, `rule.15.Parse-Override-Yes@L53404`, `rule.15.Parse-Override-No@L53420`, `rule.15.Parse-MethodSignature@L53436`, `rule.15.Parse-StateMethodSignature-Receiver@L53452`, `rule.15.Parse-MethodParams-None@L53468`
- `rule.15.Parse-MethodParams-Comma@L53484`, `rule.15.Parse-Receiver-Short-Const@L53500`, `rule.15.Parse-Receiver-Short-Unique@L53516`, `rule.15.Parse-Receiver-Short-Shared@L53532`, `rule.15.Parse-Receiver-Explicit@L53548`, `def.15.MethodDeclAst@L53566`, `def.15.ReceiverAst@L53579`, `def.15.RecordFieldsMethodsAndSelf@L53594`
- `def.15.SelfType@L53609`, `def.15.RecvType@L53622`, `def.15.RecvMode@L53638`, `def.15.RecvPerm@L53652`, `def.15.MethodSignaturesAndParams@L53667`, `rule.15.Recv-Explicit@L53686`, `rule.15.Record-Method-RecvSelf-Err@L53702`, `rule.15.Recv-Const@L53718`
- `rule.15.Recv-Unique@L53733`, `rule.15.Recv-Shared@L53748`, `rule.15.WF-Record-Method@L53763`, `rule.15.T-Record-Method-Body@L53779`, `rule.15.WF-Record-Methods@L53795`, `rule.15.Record-Method-Dup@L53811`, `def.15.ArgsOkJudg@L53827`, `def.15.RecvBaseType@L53840`
- `rule.15.Args-Empty@L53853`, `rule.15.Args-Cons@L53868`, `rule.15.Args-Cons-Ref@L53884`, `def.15.RecvArgOk@L53900`, `rule.15.T-Record-MethodCall@L53913`, `req.15.OwnerSpecificReceiverRestrictionsReuseCommonForms@L53929`, `def.15.RecvArgMode@L53944`, `def.15.MethodOf@L53958`
- `def.15.RecvBase@L53973`, `def.15.RecvParams@L53986`, `rule.15.EvalRecvSigma-Move@L54001`, `rule.15.EvalRecvSigma-Ref-Dyn@L54017`, `rule.15.EvalRecvSigma-Ref-Dyn-Expired@L54033`, `rule.15.EvalRecvSigma-Ref@L54049`, `rule.15.EvalRecvSigma-Ctrl-Move@L54065`, `rule.15.EvalRecvSigma-Ctrl-Ref@L54081`
- `def.15.BindMethodParams@L54097`, `rule.15.ApplyMethodSigma-Prim@L54110`, `rule.15.ApplyMethodSigma@L54126`, `req.15.MethodsLowerAsProceduresWithReceiverFirst@L54144`, `rule.15.Mangle-Record-Method@L54157`, `rule.15.Mangle-Class-Method@L54173`, `rule.15.Mangle-State-Method@L54189`, `diag.15.MethodsAndReceivers@L54207`
- `req.15.OverloadingNoAdditionalSyntax@L54224`, `req.15.OverloadResolutionNotParserConcern@L54239`, `def.15.ClassDefaults@L54254`, `def.15.LookupMethod@L54267`, `rule.15.LookupMethod-NotFound@L54284`, `rule.15.LookupMethod-Ambig@L54300`, `req.15.FreeProcedureOverloadResolutionBeforeCallTyping@L54316`, `req.15.FreeCallOverloadResolutionAlgorithm@L54329`
- `req.15.DuplicateErasedOverloadSignaturesForbidden@L54351`, `req.15.NoRuntimeOverloadSearch@L54366`, `req.15.OverloadResolutionCompleteBeforeLowering@L54381`, `diag-table.15.Overloading@L54396`, `diag.15.MethodLookupDiagnostics@L54413`, `grammar.15.ContractClauses@L54430`, `req.15.ForeignContractStartDisambiguatesContracts@L54452`, `rule.15.Parse-ContractClauseOpt-None@L54465`
- `rule.15.Parse-ContractClauseOpt-Yes@L54481`, `rule.15.Parse-ContractBody-PostOnly@L54497`, `rule.15.Parse-ContractBody-PrePost@L54513`, `rule.15.Parse-ContractBody-PreOnly@L54529`, `def.15.ContractClauseAst@L54547`, `def.15.ContractOpt@L54560`, `rule.15.WF-Contract@L54575`, `def.15.ContractPurityJudgementIntro@L54592`
- `rule.15.Pure-Literal@L54605`, `rule.15.Pure-Ident@L54621`, `rule.15.Pure-Field@L54637`, `rule.15.Pure-Tuple-Access@L54653`, `rule.15.Pure-Index@L54669`, `rule.15.Pure-Unary@L54685`, `rule.15.Pure-Binary@L54701`, `def.15.PureOps@L54717`
- `rule.15.Pure-Cast@L54730`, `rule.15.Pure-If@L54746`, `rule.15.Pure-If-Is@L54762`, `rule.15.Pure-If-Is-No-Else@L54778`, `rule.15.Pure-If-Case@L54794`, `rule.15.Pure-If-Case-No-Else@L54810`, `rule.15.Pure-Block@L54826`, `rule.15.Pure-Tuple@L54842`
- `rule.15.Pure-Array@L54858`, `rule.15.Pure-Record@L54874`, `rule.15.Pure-Call-Builtin@L54890`, `rule.15.Pure-Call-Procedure@L54906`, `rule.15.Pure-Method-Const@L54922`, `rule.15.Pure-Comptime@L54938`, `def.15.ContractPurityHelperPredicates@L54954`, `req.15.ContractNeverPureForms@L54974`
- `def.15.PreconditionEvaluationContext@L54987`, `def.15.PostconditionEvaluationContext@L55000`, `req.15.ContractClausesNoIndependentRuntimeEffect@L55015`, `req.15.ContractClauseLoweringViaVerificationResults@L55030`, `diag.15.ContractClauses@L55045`, `req.15.PreconditionSyntaxDefinition@L55062`, `req.15.PreconditionsParsedByContractBody@L55077`, `def.15.PreconditionOf@L55092`
- `def.15.PreconditionProofContext@L55109`, `rule.15.Pre-Satisfied@L55125`, `def.15.PreconditionElisionRules@L55141`, `req.15.CallerResponsibleForPrecondition@L55161`, `req.15.PreconditionRuntimeEvaluationOrder@L55176`, `req.15.PreconditionCheckInsertionOwnedByVerificationLogic@L55191`, `diag.15.Preconditions@L55206`, `grammar.15.Postconditions@L55223`
- `rule.15.Parse-Contract-Result@L55241`, `rule.15.Parse-Contract-Entry@L55257`, `def.15.ContractIntrinsicAst@L55275`, `def.15.PostconditionOf@L55288`, `def.15.PostconditionProofContext@L55305`, `rule.15.Post-Valid@L55320`, `def.15.PostconditionElisionRules@L55336`, `req.15.ContractResultProperties@L55356`
- `rule.15.Result-Union-Type@L55373`, `rule.15.Result-Is-Predicate@L55389`, `rule.15.Result-Narrowing@L55405`, `rule.15.Propagate-Postcondition@L55421`, `rule.15.Result-Modal@L55437`, `rule.15.Result-Generic@L55453`, `rule.15.Result-Generic-Constraint@L55469`, `req.15.ContractEntryConstraints@L55485`
- `rule.15.Entry-Type@L55503`, `req.15.PostconditionResultRuntimeBinding@L55521`, `req.15.ContractEntryRuntimeCapture@L55534`, `def.15.EntryCaptureTiming@L55551`, `rule.15.EntryCapturePhase@L55571`, `def.15.EntryCaptureValue@L55588`, `req.15.PostconditionLoweringRepresentation@L55603`, `diag.15.Postconditions@L55618`
- `grammar.15.Invariants@L55635`, `rule.15.Parse-InvariantOpt-None@L55653`, `rule.15.Parse-InvariantOpt-Yes@L55669`, `rule.15.ParseLoopInvariantOpt@L55685`, `def.15.InvariantAst@L55700`, `def.15.TypeInvariantAstExtensions@L55714`, `def.15.LoopInvariantAstPreservation@L55729`, `def.15.TypeInvariantContext@L55744`
- `def.15.TypeInvariantEnforcementPoints@L55761`, `req.15.TypeInvariantsForbidPublicMutableFields@L55778`, `req.15.PrivateProceduresExemptFromTypeInvariantPreCall@L55791`, `def.15.LoopInvariantEnforcementPoints@L55804`, `req.15.LoopInvariantExitFact@L55821`, `req.15.InvariantVerificationModeRules@L55834`, `req.15.InvariantRuntimeChecks@L55849`, `req.15.InvariantLoweringViaVerificationLogic@L55864`
- `diag.15.Invariants@L55879`, `req.15.VerificationLogicNoSurfaceSyntax@L55896`, `req.15.VerificationLogicNotParserOwned@L55911`, `def.15.ContractKind@L55926`, `def.15.VerificationFact@L55939`, `def.15.CheckState@L55952`, `def.15.ContractCheck@L55965`, `def.15.DynamicScopeAndContext@L55980`
- `rule.15.Contract-Static-OK@L56001`, `rule.15.Contract-Static-Fail@L56017`, `rule.15.Contract-Dynamic-Elide@L56033`, `rule.15.Contract-Dynamic-Check@L56049`, `req.15.MandatoryProofTechniques@L56065`, `def.15.ProofContextAt@L56085`, `def.15.DecidablePredicates@L56106`, `rule.15.Ent-True@L56126`
- `rule.15.Ent-Fact@L56142`, `rule.15.Ent-And@L56158`, `rule.15.Ent-Or-L@L56174`, `rule.15.Ent-Or-R@L56190`, `rule.15.Ent-Linear@L56206`, `def.15.LinearIntegerEntailment@L56222`, `req.15.LinearEntailmentSoundAndComplete@L56245`, `def.15.StaticProofAt@L56258`
- `def.15.NegFact@L56271`, `req.15.VerificationFactsNoRuntimeRepresentation@L56293`, `rule.15.Fact-Dominate@L56310`, `req.15.FactGeneration@L56326`, `req.15.TypeNarrowingFromFacts@L56349`, `def.15.ContractEnvironments@L56364`, `rule.15.Check-True@L56381`, `rule.15.Check-False@L56397`
- `rule.15.Check-Panic@L56413`, `rule.15.Check-Ok@L56429`, `rule.15.Check-Fail@L56445`, `req.15.DynamicChecksInjectFacts@L56461`, `def.15.RuntimeCheckInsertionPointsIntro@L56476`, `rule.15.Insert-Precondition-Check@L56489`, `rule.15.Insert-Postcondition-Check@L56505`, `rule.15.Insert-TypeInv-Construction-Check@L56521`
- `rule.15.Insert-TypeInv-PreCall-Check@L56537`, `rule.15.Insert-TypeInv-PostCall-Check@L56553`, `rule.15.Insert-LoopInv-Init-Check@L56569`, `rule.15.Insert-LoopInv-Maintenance-Check@L56585`, `rule.15.Insert-Refinement-Check@L56601`, `diag.15.VerificationLogic@L56619`, `req.15.BehavioralSubtypingNoSurfaceSyntax@L56636`, `req.15.BehavioralSubtypingNotParserOwned@L56651`
- `def.15.BehavioralSubtypingRelationship@L56666`, `req.15.BehavioralSubtypingLiskovRequirement@L56681`, `req.15.BehavioralSubtypingPreconditionRule@L56694`, `req.15.BehavioralSubtypingPostconditionRule@L56710`, `req.15.BehavioralSubtypingVerificationStrategy@L56726`, `req.15.BehavioralSubtypingNoRuntimeChecks@L56742`, `req.15.BehavioralSubtypingNoAdditionalRuntimeSemantics@L56757`, `req.15.BehavioralSubtypingLoweringNoExtraChecks@L56772`
- `diag.15.BehavioralSubtyping@L56787`, `diag.15.ProcedureContractEntryDiagnosticsOwnership@L56802`, `diag-table.15.ProcedureContractEntryDiagnostics@L56815`

### Language Constructs, Dynamic Semantics, And Feature Semantics

#### `spec.expressions`

Count: 478 total; 475 required; 0 recommended; 0 informative. Ledger line span: L56475-L64152.

- `grammar.16.LiteralAndNameExpressions@L56860`, `req.16.QualifiedApplicationOwnership@L56878`, `rule.16.Parse-Literal-Expr@L56893`, `rule.16.Parse-Null-Ptr@L56909`, `rule.16.Parse-Identifier-Expr@L56925`, `rule.16.Parse-Qualified-Name@L56941`, `def.16.LiteralKindAndToken@L56959`, `def.16.LiteralNameExprAst@L56973`
- `def.16.QualifiedNameResolution@L56987`, `def.16.ValuePathType@L57005`, `def.16.NumericLiteralTypeSets@L57028`, `def.16.NumericLiteralParsingHelpers@L57045`, `rule.16.T-Int-Literal-Suffix@L57072`, `rule.16.T-Int-Literal-Default@L57088`, `rule.16.T-Float-Literal-Explicit@L57104`, `rule.16.T-Float-Literal-Infer@L57120`
- `rule.16.T-Bool-Literal@L57136`, `rule.16.T-Char-Literal@L57152`, `rule.16.T-String-Literal@L57168`, `rule.16.Syn-Literal@L57184`, `def.16.NullLiteralExpected@L57200`, `rule.16.Chk-Int-Literal@L57213`, `rule.16.Chk-Float-Literal-Explicit@L57229`, `rule.16.Chk-Float-Literal-Infer@L57245`
- `rule.16.Chk-Null-Literal@L57261`, `def.16.PtrNullExpected@L57277`, `rule.16.Chk-Null-Ptr@L57290`, `rule.16.Syn-PtrNull-Err@L57306`, `rule.16.Chk-PtrNull-Err@L57322`, `rule.16.T-Ident@L57338`, `rule.16.T-Path-Value@L57354`, `rule.16.Expr-Unresolved-Err@L57370`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57386`, `def.16.EvaluationJudgements@L57401`, `def.16.LiteralRuntimeValues@L57416`, `rule.16.EvalSigma-Literal@L57437`, `rule.16.EvalSigma-PtrNull@L57453`, `rule.16.EvalSigma-Ident@L57468`, `rule.16.EvalSigma-Path@L57484`, `rule.16.EvalSigma-ErrorExpr@L57500`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57515`, `rule.16.Lower-Expr-Literal@L57530`, `rule.16.Lower-Expr-PtrNull@L57546`, `rule.16.Lower-Expr-Ident-Local@L57561`, `rule.16.Lower-Expr-Ident-Path@L57577`, `rule.16.Lower-Expr-Path@L57593`, `rule.16.Lower-Expr-Error@L57608`, `diag.16.LiteralAndNameExpressions@L57625`
- `grammar.16.AccessAndPlaceExpressions@L57642`, `req.16.AccessPostfixOwnership@L57658`, `rule.16.Postfix-Field@L57673`, `rule.16.Postfix-TupleIndex@L57689`, `rule.16.Postfix-Index@L57705`, `def.16.IsPlace@L57721`, `rule.16.Parse-Place-Deref@L57734`, `rule.16.Parse-Place-Postfix@L57750`
- `rule.16.Parse-Place-Err@L57766`, `def.16.AccessPlaceAst@L57784`, `def.16.PlaceForms0@L57797`, `def.16.FieldVisibility@L57810`, `def.16.IndexClassification@L57824`, `rule.16.T-Field-Record@L57841`, `rule.16.T-Field-Record-Perm@L57857`, `rule.16.P-Field-Record@L57873`
- `rule.16.P-Field-Record-Perm@L57889`, `rule.16.T-Tuple-Index@L57905`, `rule.16.T-Tuple-Index-Perm@L57921`, `rule.16.P-Tuple-Index@L57937`, `rule.16.P-Tuple-Index-Perm@L57953`, `rule.16.T-Index-Array@L57969`, `rule.16.T-Index-Array-Dynamic@L57985`, `rule.16.T-Index-Array-Perm@L58001`
- `rule.16.T-Index-Array-Perm-Dynamic@L58017`, `rule.16.T-Index-Slice@L58033`, `rule.16.T-Index-Slice-Perm@L58049`, `rule.16.T-Slice-From-Array@L58065`, `rule.16.T-Slice-From-Array-Perm@L58081`, `rule.16.T-Slice-From-Slice@L58097`, `rule.16.T-Slice-From-Slice-Perm@L58113`, `rule.16.PlaceIndexAndSliceCounterparts@L58129`
- `rule.16.Coerce-Array-Slice@L58142`, `rule.16.Union-DirectAccess-Err@L58158`, `rule.16.ValueUse-NonBitcopyPlace@L58174`, `rule.16.EvalSigma-FieldAccess@L58192`, `rule.16.EvalSigma-TupleAccess@L58208`, `rule.16.EvalSigma-Index@L58224`, `rule.16.EvalSigma-Index-Range@L58240`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58256`
- `rule.16.Lower-Expr-FieldAccess@L58271`, `rule.16.Lower-Expr-TupleAccess@L58287`, `rule.16.Lower-Expr-IndexFamily@L58303`, `rule.16.Lower-Place-Ident@L58316`, `rule.16.Lower-Place-Field@L58331`, `rule.16.Lower-Place-Tuple@L58347`, `rule.16.Lower-Place-Index@L58363`, `rule.16.Lower-Place-Deref@L58379`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58395`, `diag.16.AccessAndPlaceExpressions@L58410`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58423`, `grammar.16.CallExpressions@L58440`, `req.16.QualifiedApplyParenPreResolution@L58459`, `rule.16.Postfix-Call@L58474`, `rule.16.Postfix-Call-TypeArgs@L58490`, `rule.16.Postfix-MethodCall@L58506`
- `rule.16.Parse-Qualified-Apply-Paren@L58522`, `rule.16.ArgumentListParsingFamily@L58538`, `def.16.ArgAst@L58553`, `def.16.CallExprAst@L58566`, `def.16.ArgAccessors@L58579`, `def.16.MovedArg@L58593`, `req.16.QualifiedParenthesizedApplicationResolution@L58608`, `def.16.CallStaticJudgementsAndArgumentTyping@L58627`
- `rule.16.ArgsT-Empty@L58655`, `rule.16.ArgsT-Cons@L58670`, `rule.16.ArgsT-Cons-Ref@L58686`, `rule.16.T-Call-Generic-Infer@L58702`, `rule.16.T-Call@L58718`, `rule.16.Call-Callee-NotFunc@L58734`, `rule.16.Call-ArgCount-Err@L58750`, `rule.16.Call-ArgType-Err@L58766`
- `rule.16.Call-Move-Missing@L58782`, `rule.16.Call-Move-Unexpected@L58798`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58814`, `rule.16.Call-Arg-NotPlace@L58830`, `rule.16.Chk-Call-Generic-Infer@L58846`, `req.16.CallTypeArgsStaticOwnership@L58862`, `req.16.MethodRecordClosureCallStaticOwnership@L58875`, `req.16.ExternProcedureCallsRequireUnsafe@L58888`
- `rule.16.EvalSigma-Call-Closure@L58903`, `rule.16.EvalSigma-Call-RegionProc@L58919`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58935`, `rule.16.EvalSigma-Call-CancelProc@L58951`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58967`, `rule.16.EvalSigma-Call-Proc@L58983`, `rule.16.EvalSigma-Call-Record@L58999`, `rule.16.EvalSigma-MethodCall@L59015`
- `req.16.CallControlPropagation@L59031`, `req.16.MethodCallControlPropagation@L59044`, `req.16.CallTypeArgsEvaluationElaboration@L59057`, `rule.16.Lower-Args-Empty@L59072`, `rule.16.Lower-Args-Cons-Move@L59087`, `rule.16.Lower-Args-Cons-Ref@L59103`, `rule.16.Lower-Expr-Call-Closure@L59119`, `rule.16.Lower-Expr-CallFamily@L59135`
- `rule.16.Lower-MethodCallFamily@L59148`, `req.16.CallTypeArgsLoweringElaboration@L59161`, `diag.16.CallExpressions@L59176`, `grammar.16.OperatorExpressions@L59193`, `req.16.OperatorPrefixSyntaxOwnership@L59224`, `rule.16.ParseRangeFamily@L59239`, `rule.16.ParseLeftChainFamily@L59252`, `rule.16.ParsePowerFamily@L59265`
- `rule.16.Parse-Unary-Prefix@L59278`, `def.16.RangeAndOperatorExprAst@L59296`, `def.16.OperatorSets@L59310`, `def.16.OperatorStaticTypes@L59328`, `rule.16.T-Range-Lift@L59343`, `rule.16.RangeTypingFamily@L59359`, `rule.16.T-Not-Bool@L59372`, `rule.16.T-Not-Int@L59388`
- `rule.16.T-Neg@L59404`, `rule.16.T-Arith@L59420`, `rule.16.T-Bitwise@L59436`, `rule.16.T-Shift@L59452`, `rule.16.T-Compare-Eq@L59468`, `rule.16.T-Compare-Ord@L59484`, `rule.16.T-Logical@L59500`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59518`
- `def.16.OperatorComparisonRuntime@L59540`, `def.16.OperatorBitShiftArithmeticRuntime@L59560`, `def.16.UnaryOperatorRuntime@L59580`, `req.16.FloatUnaryNegationTotality@L59598`, `def.16.BinaryOperatorRuntime@L59611`, `rule.16.EvalSigma-Range@L59632`, `rule.16.EvalSigma-Unary@L59648`, `rule.16.EvalSigma-Bin-And-False@L59664`
- `rule.16.EvalSigma-Bin-And-True@L59680`, `rule.16.EvalSigma-Bin-Or-True@L59696`, `rule.16.EvalSigma-Bin-Or-False@L59712`, `rule.16.EvalSigma-Binary@L59728`, `req.16.OperatorUndefinedAndNaNBehavior@L59744`, `rule.16.Lower-Expr-Unary@L59759`, `rule.16.Lower-Expr-Bin-And@L59775`, `rule.16.Lower-Expr-Bin-Or@L59791`
- `rule.16.Lower-Expr-Binary@L59807`, `rule.16.Lower-Expr-Range@L59823`, `def.16.UnaryOperatorLoweringPanicCheck@L59839`, `rule.16.Lower-UnOp-Ok@L59853`, `rule.16.Lower-UnOp-Panic@L59869`, `req.16.UnaryNegationLoweringOverflowChecks@L59885`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59898`, `diag.16.OperatorExpressions@L59913`
- `grammar.16.CastAndTransmuteExpressions@L59930`, `req.16.WidenPrefixOwnershipForCastTransmute@L59946`, `rule.16.Parse-Cast@L59961`, `rule.16.Parse-CastTail-None@L59977`, `rule.16.Parse-CastTail-As@L59993`, `rule.16.ParseTransmuteExprFamily@L60009`, `req.16.WidenParsingOwnershipForCastTransmute@L60022`, `def.16.CastTransmuteExprAst@L60037`
- `req.16.WidenAstOwnershipForCastTransmute@L60050`, `def.16.CastValidity@L60065`, `rule.16.T-Cast@L60080`, `rule.16.T-Cast-Invalid@L60096`, `rule.16.T-Transmute-SizeEq@L60112`, `rule.16.T-Transmute-AlignEq@L60128`, `rule.16.T-Transmute@L60144`, `rule.16.Transmute-Unsafe-Err@L60160`
- `def.16.ValidTransmuteTarget@L60176`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60193`, `def.16.CastDynamicContext@L60208`, `def.16.CastRuntimeConversionHelpers@L60222`, `rule.16.CastVal-Id@L60256`, `rule.16.CastVal-Int-Int-Signed@L60272`, `rule.16.CastVal-Int-Int-Unsigned@L60288`, `rule.16.CastVal-Int-Float@L60304`
- `req.16.IntToFloatLoweringPreservesSignedness@L60320`, `rule.16.CastVal-Float-Float@L60333`, `rule.16.CastVal-Float-Int@L60349`, `rule.16.CastVal-Bool-Int@L60365`, `rule.16.CastVal-Int-Bool@L60383`, `rule.16.CastVal-Char-U32@L60401`, `rule.16.CastVal-U32-Char@L60417`, `rule.16.EvalSigma-Cast@L60433`
- `rule.16.EvalSigma-Cast-Panic@L60449`, `def.16.TransmuteVal@L60465`, `rule.16.EvalSigma-Transmute@L60478`, `rule.16.EvalSigma-Transmute-Ctrl@L60494`, `req.16.WidenDynamicOwnershipForCastTransmute@L60510`, `rule.16.Lower-Expr-Cast@L60525`, `rule.16.Lower-Expr-Transmute@L60541`, `rule.16.LowerCastTransmuteFamily@L60557`
- `diag.16.CastAndTransmuteExpressions@L60572`, `grammar.16.ConstructionExpressions@L60589`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60611`, `rule.16.Parse-Tuple-Literal@L60626`, `rule.16.Parse-Array-Segment-Elem@L60642`, `rule.16.Parse-Array-Segment-Repeat@L60658`, `rule.16.Parse-Array-Segment-List-Empty@L60674`, `rule.16.Parse-Array-Segment-List-Single@L60689`
- `rule.16.Parse-Array-Segment-List-Comma@L60705`, `rule.16.Parse-Array-Literal@L60721`, `rule.16.Parse-Record-Literal-ModalState@L60737`, `rule.16.Parse-Record-Literal@L60753`, `rule.16.Parse-Qualified-Apply-Brace@L60769`, `rule.16.ConstructionListAndShorthandParsingFamily@L60785`, `def.16.FieldInitAst@L60800`, `def.16.ConstructionExprAst@L60813`
- `def.16.FieldInitNamesAndSet@L60826`, `req.16.QualifiedBraceApplicationResolution@L60840`, `req.16.QualifiedParenApplicationConstructionResolution@L60856`, `rule.16.T-Unit-Literal@L60871`, `rule.16.T-Tuple-Literal@L60886`, `def.16.ArraySegmentLength@L60902`, `rule.16.T-Array-Literal-Segments@L60916`, `def.16.RecordFieldNameSet@L60941`
- `rule.16.T-Record-Literal@L60955`, `rule.16.Record-FieldInit-Dup@L60971`, `rule.16.Record-FieldInit-Missing@L60987`, `rule.16.RecordFieldUnknownNotVisibleFamily@L61003`, `rule.16.Record-Field-NonBitcopy-Move@L61016`, `rule.16.EnumLiteralTypingFamily@L61032`, `def.16.RecordDefaultConstructionEligibility@L61045`, `rule.16.T-Record-Default@L61059`
- `rule.16.Record-Default-Init-Err@L61075`, `rule.16.EvalSigmaTupleConstructionFamily@L61093`, `rule.16.EvalSigmaArrayConstructionFamily@L61106`, `rule.16.EvalSigmaRecordConstructionFamily@L61119`, `rule.16.EvalSigmaEnumConstructionFamily@L61132`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61145`, `rule.16.Lower-Expr-Tuple@L61160`, `rule.16.Lower-Expr-Array@L61176`
- `rule.16.Lower-Expr-Record@L61192`, `rule.16.LowerEnumConstructionFamily@L61208`, `rule.16.Lower-CallIR-RecordCtor@L61221`, `diag.16.ConstructionExpressions@L61239`, `grammar.16.ControlExpressions@L61256`, `req.16.ControlExpressionOwnership@L61280`, `rule.16.Parse-If-Expr@L61296`, `rule.16.Parse-If-Is-Single@L61312`
- `rule.16.Parse-If-Is-CaseList@L61328`, `rule.16.Parse-Loop-Expr@L61344`, `rule.16.Parse-Block-Expr@L61360`, `rule.16.ControlExpressionParsingRemainderFamily@L61376`, `def.16.ControlExprAst@L61391`, `def.16.ControlAstHelpers@L61404`, `def.16.LoopTypeInference@L61418`, `req.16.BlockTypingOwnershipForControlExpressions@L61442`
- `rule.16.T-If@L61461`, `rule.16.T-If-No-Else@L61477`, `rule.16.CheckIfFamily@L61493`, `req.16.PatternTypingOwnershipForControlExpressions@L61506`, `rule.16.T-If-Is@L61522`, `rule.16.T-If-Is-No-Else@L61538`, `rule.16.IfCaseTypingFamily@L61554`, `rule.16.CheckIfIsAndIfCaseFamily@L61567`
- `req.16.LoopInvariantTypingOwnership@L61581`, `rule.16.T-Loop-Infinite@L61594`, `rule.16.T-Loop-Conditional@L61610`, `rule.16.T-Loop-Iter@L61626`, `rule.16.AsyncIteratorLoopTypingFamily@L61642`, `rule.16.EvalSigma-If-True@L61657`, `rule.16.EvalSigma-If-False-None@L61673`, `rule.16.EvalSigma-If-False-Some@L61689`
- `rule.16.EvalSigma-If-Ctrl@L61705`, `rule.16.EvalSigma-If-Is@L61721`, `rule.16.EvalSigma-If-Is-Ctrl@L61737`, `rule.16.EvalSigma-If-Cases@L61753`, `rule.16.EvalSigma-If-Cases-Ctrl@L61769`, `rule.16.EvalIfCasesFamily@L61785`, `rule.16.EvalSigma-Block@L61798`, `def.16.LoopIterableTypePredicates@L61814`
- `def.16.LoopIteratorRuntime@L61832`, `def.16.LoopIterJudgement@L61865`, `rule.16.EvalSigma-Loop-Infinite-Step@L61878`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61894`, `rule.16.EvalSigma-Loop-Infinite-Break@L61910`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61926`, `rule.16.EvalSigma-Loop-Cond-False@L61942`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61958`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61974`, `rule.16.EvalSigma-Loop-Cond-Break@L61990`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L62006`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L62022`, `rule.16.EvalSigma-Loop-Iter@L62038`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L62054`, `rule.16.LoopIter-Done@L62070`, `rule.16.LoopIter-Step-Val@L62086`
- `rule.16.LoopIter-Step-Continue@L62102`, `rule.16.LoopIter-Step-Break@L62118`, `rule.16.LoopIter-Step-Ctrl@L62134`, `rule.16.Lower-Expr-If@L62152`, `rule.16.Lower-Expr-If-Is@L62168`, `rule.16.Lower-Expr-If-Cases@L62184`, `rule.16.LowerLoopExpressionFamily@L62200`, `rule.16.Lower-Expr-Block@L62213`
- `req.16.ControlExpressionLoweringOwnership@L62229`, `diag.16.ControlExpressions@L62244`, `req.16.ControlExpressionDiagnosticOwnership@L62257`, `grammar.16.EffectfulCoreExpressions@L62275`, `req.16.RegionAliasAllocRewrite@L62295`, `rule.16.Parse-Unary-Deref@L62310`, `rule.16.Parse-Unary-AddressOf@L62326`, `rule.16.Parse-Unary-Move@L62342`
- `rule.16.Postfix-Propagate@L62358`, `rule.16.Parse-Alloc-Implicit@L62374`, `rule.16.Parse-Unsafe-Expr@L62390`, `def.16.EffectfulCoreExprAst@L62408`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62421`, `def.16.AddressOfStaticHelpers@L62440`, `rule.16.T-Unsafe-Expr@L62455`, `rule.16.Chk-Unsafe-Expr@L62471`
- `rule.16.T-AddrOf@L62487`, `rule.16.T-Deref-Ptr@L62503`, `rule.16.T-Deref-Raw@L62519`, `rule.16.DerefPlaceTypingFamily@L62535`, `rule.16.T-Move@L62548`, `rule.16.T-Alloc-Explicit@L62564`, `rule.16.T-Alloc-Implicit@L62580`, `def.16.SuccessMember@L62596`
- `rule.16.T-Propagate@L62609`, `def.16.SuccessMemberAsync@L62625`, `rule.16.T-Async-Try@L62638`, `rule.16.Async-Try-Infallible-Err@L62654`, `rule.16.EvalSigma-UnsafeBlock@L62672`, `rule.16.EvalSigma-AddressOf@L62688`, `rule.16.EvalSigma-Deref@L62704`, `rule.16.EvalSigma-Move@L62720`
- `rule.16.EvalSigma-Alloc-Implicit@L62736`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62752`, `rule.16.EvalSigma-Alloc-Explicit@L62768`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62784`, `rule.16.EvalSigma-Propagate-Success@L62800`, `rule.16.EvalSigma-Propagate-Success-Async@L62816`, `rule.16.EvalSigma-Propagate-Error@L62832`, `rule.16.EvalSigma-Propagate-Error-Async@L62848`
- `rule.16.EvalSigma-Propagate-Ctrl@L62865`, `def.16.ExprStateAndTerminalExpr@L62881`, `rule.16.StepSigma-Pure@L62896`, `rule.16.StepSigma-Alloc-Implicit@L62912`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62928`, `rule.16.StepSigma-Alloc-Explicit@L62944`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62960`, `rule.16.StepSigma-Block@L62976`
- `rule.16.StepSigma-UnsafeBlock@L62992`, `rule.16.StepSigma-Loop@L63008`, `rule.16.StepSigma-Stateful-Other@L63024`, `rule.16.Lower-Expr-UnsafeBlock@L63042`, `rule.16.Lower-Expr-Move@L63058`, `rule.16.Lower-Expr-AddressOf@L63074`, `rule.16.Lower-Expr-Deref@L63090`, `rule.16.Lower-Expr-Alloc@L63106`
- `rule.16.Lower-Expr-Propagate-Success@L63122`, `rule.16.Lower-Expr-Propagate-Return@L63138`, `req.16.EffectfulCoreLoweringMechanics@L63154`, `diag.16.EffectfulCoreExpressions@L63169`, `grammar.16.ClosureAndPipelineExpressions@L63186`, `req.16.ClosureParamTrailingComma@L63205`, `req.16.ClosureUnionParamParentheses@L63218`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63231`
- `rule.16.Parse-Pipeline@L63246`, `rule.16.Parse-PipelineTail-Stop@L63262`, `rule.16.Parse-PipelineTail-Cons@L63278`, `rule.16.Parse-Closure-Expr@L63294`, `rule.16.Parse-Closure-Expr-Empty@L63310`, `rule.16.Parse-ClosureParams-Single@L63326`, `rule.16.Parse-ClosureParams-Cons@L63342`, `rule.16.Parse-ClosureParamType-Grouped@L63358`
- `rule.16.Parse-ClosureParamType-Plain@L63374`, `rule.16.Parse-ClosureParam-MoveTyped@L63390`, `rule.16.Parse-ClosureParam-MoveUntyped@L63406`, `rule.16.Parse-ClosureParam-Typed@L63422`, `rule.16.Parse-ClosureParam-Untyped@L63438`, `rule.16.Parse-ClosureRetOpt-Some@L63454`, `rule.16.Parse-ClosureRetOpt-None@L63470`, `rule.16.Parse-ClosureBody-Block@L63486`
- `rule.16.Parse-ClosureBody-Expr@L63502`, `def.16.ClosurePipelineAstForms@L63520`, `def.16.ClosureCaptureSets@L63539`, `def.16.ClosureEscapeClassification@L63559`, `def.16.ClosureParameterAccessors@L63574`, `rule.16.T-Closure-NonCapturing@L63588`, `rule.16.T-Closure-Capturing@L63606`, `rule.16.T-Closure-Escaping@L63625`
- `rule.16.K-Closure-Escape-Type@L63645`, `rule.16.Capture-Const@L63661`, `rule.16.Capture-Shared@L63677`, `rule.16.Capture-Unique-Err@L63693`, `rule.16.T-ClosureCall@L63709`, `rule.16.Infer-Closure-Params@L63725`, `rule.16.Infer-Closure-Params-Err@L63741`, `rule.16.Infer-Closure-Return@L63757`
- `req.16.ClosureSharedDependencyInference@L63773`, `def.16.ClosureCaptureBindingAccessors@L63786`, `rule.16.B-Closure-NonCapturing@L63810`, `rule.16.B-Closure-Capturing@L63826`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63845`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63862`, `rule.16.B-Closure-RefCapture-Moved-Err@L63879`, `rule.16.T-Pipeline@L63896`
- `rule.16.T-Pipeline-NotCallable-Err@L63914`, `rule.16.T-Pipeline-TypeMismatch-Err@L63931`, `rule.16.T-Pipeline-ArgCount-Err@L63949`, `rule.16.B-Pipeline@L63966`, `req.16.ClosureParamInferenceFailure@L63982`, `req.16.ClosureSharedDependencyInferenceRestated@L63995`, `def.16.ClosureEnvironmentRuntimeModel@L64010`, `rule.16.EvalSigma-Closure-NonCapturing@L64046`
- `rule.16.EvalSigma-Closure-Capturing@L64062`, `def.16.MarkMoved@L64080`, `rule.16.EvalSigma-ClosureCall@L64094`, `def.16.ClosureCallRuntimeHelpers@L64112`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64134`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64150`, `req.16.ClosureCallResolvedInternalFormRuntime@L64167`, `req.16.PipelineDesugaring@L64180`
- `rule.16.EvalSigma-Pipeline-Func@L64193`, `rule.16.EvalSigma-Pipeline-Closure@L64210`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64227`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64243`, `def.16.ClosureLoweringCaptureTypes@L64261`, `rule.16.Layout-ClosureEnv@L64277`, `rule.16.Layout-ClosureEnv-Empty@L64293`, `rule.16.Lower-Expr-Closure-NonCapturing@L64309`
- `rule.16.Lower-Expr-Closure-Capturing@L64325`, `def.16.LowerCaptureEnv@L64343`, `def.16.CapturedIdentifierLoweringHelpers@L64363`, `rule.16.Lower-CapturedIdent-Ref@L64378`, `req.16.LowerCapturedIdentRefTemporaries@L64395`, `rule.16.Lower-CapturedIdent-Move@L64408`, `def.16.ClosureEnvParam@L64425`, `def.16.ClosureCodeSig@L64438`
- `rule.16.Lower-Closure-Call@L64455`, `req.16.LowerClosureCallResolvedInternalForm@L64473`, `rule.16.Lower-Expr-Pipeline@L64486`, `def.16.LowerPipelineCallablePredicates@L64504`, `diag.16.ClosureAndPipelineExpressions@L64522`, `diag.16.ExpressionDiagnosticsSupplement@L64537`
- `grammar.16.LiteralAndNameExpressions@L56860`, `req.16.QualifiedApplicationOwnership@L56878`, `rule.16.Parse-Literal-Expr@L56893`, `rule.16.Parse-Null-Ptr@L56909`, `rule.16.Parse-Identifier-Expr@L56925`, `rule.16.Parse-Qualified-Name@L56941`, `def.16.LiteralKindAndToken@L56959`, `def.16.LiteralNameExprAst@L56973`
- `def.16.QualifiedNameResolution@L56987`, `def.16.ValuePathType@L57005`, `def.16.NumericLiteralTypeSets@L57028`, `def.16.NumericLiteralParsingHelpers@L57045`, `rule.16.T-Int-Literal-Suffix@L57072`, `rule.16.T-Int-Literal-Default@L57088`, `rule.16.T-Float-Literal-Explicit@L57104`, `rule.16.T-Float-Literal-Infer@L57120`
- `rule.16.T-Bool-Literal@L57136`, `rule.16.T-Char-Literal@L57152`, `rule.16.T-String-Literal@L57168`, `rule.16.Syn-Literal@L57184`, `def.16.NullLiteralExpected@L57200`, `rule.16.Chk-Int-Literal@L57213`, `rule.16.Chk-Float-Literal-Explicit@L57229`, `rule.16.Chk-Float-Literal-Infer@L57245`
- `rule.16.Chk-Null-Literal@L57261`, `def.16.PtrNullExpected@L57277`, `rule.16.Chk-Null-Ptr@L57290`, `rule.16.Syn-PtrNull-Err@L57306`, `rule.16.Chk-PtrNull-Err@L57322`, `rule.16.T-Ident@L57338`, `rule.16.T-Path-Value@L57354`, `rule.16.Expr-Unresolved-Err@L57370`
- `req.16.QualifiedNameEliminatedBeforeTyping@L57386`, `def.16.EvaluationJudgements@L57401`, `def.16.LiteralRuntimeValues@L57416`, `rule.16.EvalSigma-Literal@L57437`, `rule.16.EvalSigma-PtrNull@L57453`, `rule.16.EvalSigma-Ident@L57468`, `rule.16.EvalSigma-Path@L57484`, `rule.16.EvalSigma-ErrorExpr@L57500`
- `req.16.NamePathEvaluationMayPanicForPoisonedModules@L57515`, `rule.16.Lower-Expr-Literal@L57530`, `rule.16.Lower-Expr-PtrNull@L57546`, `rule.16.Lower-Expr-Ident-Local@L57561`, `rule.16.Lower-Expr-Ident-Path@L57577`, `rule.16.Lower-Expr-Path@L57593`, `rule.16.Lower-Expr-Error@L57608`, `diag.16.LiteralAndNameExpressions@L57625`
- `grammar.16.AccessAndPlaceExpressions@L57642`, `req.16.AccessPostfixOwnership@L57658`, `rule.16.Postfix-Field@L57673`, `rule.16.Postfix-TupleIndex@L57689`, `rule.16.Postfix-Index@L57705`, `def.16.IsPlace@L57721`, `rule.16.Parse-Place-Deref@L57734`, `rule.16.Parse-Place-Postfix@L57750`
- `rule.16.Parse-Place-Err@L57766`, `def.16.AccessPlaceAst@L57784`, `def.16.PlaceForms0@L57797`, `def.16.FieldVisibility@L57810`, `def.16.IndexClassification@L57824`, `rule.16.T-Field-Record@L57841`, `rule.16.T-Field-Record-Perm@L57857`, `rule.16.P-Field-Record@L57873`
- `rule.16.P-Field-Record-Perm@L57889`, `rule.16.T-Tuple-Index@L57905`, `rule.16.T-Tuple-Index-Perm@L57921`, `rule.16.P-Tuple-Index@L57937`, `rule.16.P-Tuple-Index-Perm@L57953`, `rule.16.T-Index-Array@L57969`, `rule.16.T-Index-Array-Dynamic@L57985`, `rule.16.T-Index-Array-Perm@L58001`
- `rule.16.T-Index-Array-Perm-Dynamic@L58017`, `rule.16.T-Index-Slice@L58033`, `rule.16.T-Index-Slice-Perm@L58049`, `rule.16.T-Slice-From-Array@L58065`, `rule.16.T-Slice-From-Array-Perm@L58081`, `rule.16.T-Slice-From-Slice@L58097`, `rule.16.T-Slice-From-Slice-Perm@L58113`, `rule.16.PlaceIndexAndSliceCounterparts@L58129`
- `rule.16.Coerce-Array-Slice@L58142`, `rule.16.Union-DirectAccess-Err@L58158`, `rule.16.ValueUse-NonBitcopyPlace@L58174`, `rule.16.EvalSigma-FieldAccess@L58192`, `rule.16.EvalSigma-TupleAccess@L58208`, `rule.16.EvalSigma-Index@L58224`, `rule.16.EvalSigma-Index-Range@L58240`, `req.16.IndexAccessRuntimeFailuresAndControlPropagation@L58256`
- `rule.16.Lower-Expr-FieldAccess@L58271`, `rule.16.Lower-Expr-TupleAccess@L58287`, `rule.16.Lower-Expr-IndexFamily@L58303`, `rule.16.Lower-Place-Ident@L58316`, `rule.16.Lower-Place-Field@L58331`, `rule.16.Lower-Place-Tuple@L58347`, `rule.16.Lower-Place-Index@L58363`, `rule.16.Lower-Place-Deref@L58379`
- `req.16.PlaceReadWriteLoweringPreservesAccessBehavior@L58395`, `diag.16.AccessAndPlaceExpressions@L58410`, `req.16.ArraySliceIndexDiagnosticsAndPanicBehavior@L58423`, `grammar.16.CallExpressions@L58440`, `req.16.QualifiedApplyParenPreResolution@L58459`, `rule.16.Postfix-Call@L58474`, `rule.16.Postfix-Call-TypeArgs@L58490`, `rule.16.Postfix-MethodCall@L58506`
- `rule.16.Parse-Qualified-Apply-Paren@L58522`, `rule.16.ArgumentListParsingFamily@L58538`, `def.16.ArgAst@L58553`, `def.16.CallExprAst@L58566`, `def.16.ArgAccessors@L58579`, `def.16.MovedArg@L58593`, `req.16.QualifiedParenthesizedApplicationResolution@L58608`, `def.16.CallStaticJudgementsAndArgumentTyping@L58627`
- `rule.16.ArgsT-Empty@L58655`, `rule.16.ArgsT-Cons@L58670`, `rule.16.ArgsT-Cons-Ref@L58686`, `rule.16.T-Call-Generic-Infer@L58702`, `rule.16.T-Call@L58718`, `rule.16.Call-Callee-NotFunc@L58734`, `rule.16.Call-ArgCount-Err@L58750`, `rule.16.Call-ArgType-Err@L58766`
- `rule.16.Call-Move-Missing@L58782`, `rule.16.Call-Move-Unexpected@L58798`, `rule.16.Call-Arg-Packed-Unsafe-Err@L58814`, `rule.16.Call-Arg-NotPlace@L58830`, `rule.16.Chk-Call-Generic-Infer@L58846`, `req.16.CallTypeArgsStaticOwnership@L58862`, `req.16.MethodRecordClosureCallStaticOwnership@L58875`, `req.16.ExternProcedureCallsRequireUnsafe@L58888`
- `rule.16.EvalSigma-Call-Closure@L58903`, `rule.16.EvalSigma-Call-RegionProc@L58919`, `rule.16.EvalSigma-Call-RegionProc-Ctrl-Args@L58935`, `rule.16.EvalSigma-Call-CancelProc@L58951`, `rule.16.EvalSigma-Call-CancelProc-Ctrl-Args@L58967`, `rule.16.EvalSigma-Call-Proc@L58983`, `rule.16.EvalSigma-Call-Record@L58999`, `rule.16.EvalSigma-MethodCall@L59015`
- `req.16.CallControlPropagation@L59031`, `req.16.MethodCallControlPropagation@L59044`, `req.16.CallTypeArgsEvaluationElaboration@L59057`, `rule.16.Lower-Args-Empty@L59072`, `rule.16.Lower-Args-Cons-Move@L59087`, `rule.16.Lower-Args-Cons-Ref@L59103`, `rule.16.Lower-Expr-Call-Closure@L59119`, `rule.16.Lower-Expr-CallFamily@L59135`
- `rule.16.Lower-MethodCallFamily@L59148`, `req.16.CallTypeArgsLoweringElaboration@L59161`, `diag.16.CallExpressions@L59176`, `grammar.16.OperatorExpressions@L59193`, `req.16.OperatorPrefixSyntaxOwnership@L59224`, `rule.16.ParseRangeFamily@L59239`, `rule.16.ParseLeftChainFamily@L59252`, `rule.16.ParsePowerFamily@L59265`
- `rule.16.Parse-Unary-Prefix@L59278`, `def.16.RangeAndOperatorExprAst@L59296`, `def.16.OperatorSets@L59310`, `def.16.OperatorStaticTypes@L59328`, `rule.16.T-Range-Lift@L59343`, `rule.16.RangeTypingFamily@L59359`, `rule.16.T-Not-Bool@L59372`, `rule.16.T-Not-Int@L59388`
- `rule.16.T-Neg@L59404`, `rule.16.T-Arith@L59420`, `rule.16.T-Bitwise@L59436`, `rule.16.T-Shift@L59452`, `rule.16.T-Compare-Eq@L59468`, `rule.16.T-Compare-Ord@L59484`, `rule.16.T-Logical@L59500`, `def.16.OperatorRuntimeJudgementsAndValuePredicates@L59518`
- `def.16.OperatorComparisonRuntime@L59540`, `def.16.OperatorBitShiftArithmeticRuntime@L59560`, `def.16.UnaryOperatorRuntime@L59580`, `req.16.FloatUnaryNegationTotality@L59598`, `def.16.BinaryOperatorRuntime@L59611`, `rule.16.EvalSigma-Range@L59632`, `rule.16.EvalSigma-Unary@L59648`, `rule.16.EvalSigma-Bin-And-False@L59664`
- `rule.16.EvalSigma-Bin-And-True@L59680`, `rule.16.EvalSigma-Bin-Or-True@L59696`, `rule.16.EvalSigma-Bin-Or-False@L59712`, `rule.16.EvalSigma-Binary@L59728`, `req.16.OperatorUndefinedAndNaNBehavior@L59744`, `rule.16.Lower-Expr-Unary@L59759`, `rule.16.Lower-Expr-Bin-And@L59775`, `rule.16.Lower-Expr-Bin-Or@L59791`
- `rule.16.Lower-Expr-Binary@L59807`, `rule.16.Lower-Expr-Range@L59823`, `def.16.UnaryOperatorLoweringPanicCheck@L59839`, `rule.16.Lower-UnOp-Ok@L59853`, `rule.16.Lower-UnOp-Panic@L59869`, `req.16.UnaryNegationLoweringOverflowChecks@L59885`, `rule.16.LowerBinaryAndRangeRemainderFamily@L59898`, `diag.16.OperatorExpressions@L59913`
- `grammar.16.CastAndTransmuteExpressions@L59930`, `req.16.WidenPrefixOwnershipForCastTransmute@L59946`, `rule.16.Parse-Cast@L59961`, `rule.16.Parse-CastTail-None@L59977`, `rule.16.Parse-CastTail-As@L59993`, `rule.16.ParseTransmuteExprFamily@L60009`, `req.16.WidenParsingOwnershipForCastTransmute@L60022`, `def.16.CastTransmuteExprAst@L60037`
- `req.16.WidenAstOwnershipForCastTransmute@L60050`, `def.16.CastValidity@L60065`, `rule.16.T-Cast@L60080`, `rule.16.T-Cast-Invalid@L60096`, `rule.16.T-Transmute-SizeEq@L60112`, `rule.16.T-Transmute-AlignEq@L60128`, `rule.16.T-Transmute@L60144`, `rule.16.Transmute-Unsafe-Err@L60160`
- `def.16.ValidTransmuteTarget@L60176`, `req.16.WidenTypingDiagnosticsOwnershipForCastTransmute@L60193`, `def.16.CastDynamicContext@L60208`, `def.16.CastRuntimeConversionHelpers@L60222`, `rule.16.CastVal-Id@L60256`, `rule.16.CastVal-Int-Int-Signed@L60272`, `rule.16.CastVal-Int-Int-Unsigned@L60288`, `rule.16.CastVal-Int-Float@L60304`
- `req.16.IntToFloatLoweringPreservesSignedness@L60320`, `rule.16.CastVal-Float-Float@L60333`, `rule.16.CastVal-Float-Int@L60349`, `rule.16.CastVal-Bool-Int@L60365`, `rule.16.CastVal-Int-Bool@L60383`, `rule.16.CastVal-Char-U32@L60401`, `rule.16.CastVal-U32-Char@L60417`, `rule.16.EvalSigma-Cast@L60433`
- `rule.16.EvalSigma-Cast-Panic@L60449`, `def.16.TransmuteVal@L60465`, `rule.16.EvalSigma-Transmute@L60478`, `rule.16.EvalSigma-Transmute-Ctrl@L60494`, `req.16.WidenDynamicOwnershipForCastTransmute@L60510`, `rule.16.Lower-Expr-Cast@L60525`, `rule.16.Lower-Expr-Transmute@L60541`, `rule.16.LowerCastTransmuteFamily@L60557`
- `diag.16.CastAndTransmuteExpressions@L60572`, `grammar.16.ConstructionExpressions@L60589`, `req.16.EnumConstructorAndRecordDefaultSyntaxResolution@L60611`, `rule.16.Parse-Tuple-Literal@L60626`, `rule.16.Parse-Array-Segment-Elem@L60642`, `rule.16.Parse-Array-Segment-Repeat@L60658`, `rule.16.Parse-Array-Segment-List-Empty@L60674`, `rule.16.Parse-Array-Segment-List-Single@L60689`
- `rule.16.Parse-Array-Segment-List-Comma@L60705`, `rule.16.Parse-Array-Literal@L60721`, `rule.16.Parse-Record-Literal-ModalState@L60737`, `rule.16.Parse-Record-Literal@L60753`, `rule.16.Parse-Qualified-Apply-Brace@L60769`, `rule.16.ConstructionListAndShorthandParsingFamily@L60785`, `def.16.FieldInitAst@L60800`, `def.16.ConstructionExprAst@L60813`
- `def.16.FieldInitNamesAndSet@L60826`, `req.16.QualifiedBraceApplicationResolution@L60840`, `req.16.QualifiedParenApplicationConstructionResolution@L60856`, `rule.16.T-Unit-Literal@L60871`, `rule.16.T-Tuple-Literal@L60886`, `def.16.ArraySegmentLength@L60902`, `rule.16.T-Array-Literal-Segments@L60916`, `def.16.RecordFieldNameSet@L60941`
- `rule.16.T-Record-Literal@L60955`, `rule.16.Record-FieldInit-Dup@L60971`, `rule.16.Record-FieldInit-Missing@L60987`, `rule.16.RecordFieldUnknownNotVisibleFamily@L61003`, `rule.16.Record-Field-NonBitcopy-Move@L61016`, `rule.16.EnumLiteralTypingFamily@L61032`, `def.16.RecordDefaultConstructionEligibility@L61045`, `rule.16.T-Record-Default@L61059`
- `rule.16.Record-Default-Init-Err@L61075`, `rule.16.EvalSigmaTupleConstructionFamily@L61093`, `rule.16.EvalSigmaArrayConstructionFamily@L61106`, `rule.16.EvalSigmaRecordConstructionFamily@L61119`, `rule.16.EvalSigmaEnumConstructionFamily@L61132`, `req.16.RecordDefaultConstructionRuntimeUsesCallRecord@L61145`, `rule.16.Lower-Expr-Tuple@L61160`, `rule.16.Lower-Expr-Array@L61176`
- `rule.16.Lower-Expr-Record@L61192`, `rule.16.LowerEnumConstructionFamily@L61208`, `rule.16.Lower-CallIR-RecordCtor@L61221`, `diag.16.ConstructionExpressions@L61239`, `grammar.16.ControlExpressions@L61256`, `req.16.ControlExpressionOwnership@L61280`, `rule.16.Parse-If-Expr@L61296`, `rule.16.Parse-If-Is-Single@L61312`
- `rule.16.Parse-If-Is-CaseList@L61328`, `rule.16.Parse-Loop-Expr@L61344`, `rule.16.Parse-Block-Expr@L61360`, `rule.16.ControlExpressionParsingRemainderFamily@L61376`, `def.16.ControlExprAst@L61391`, `def.16.ControlAstHelpers@L61404`, `def.16.LoopTypeInference@L61418`, `req.16.BlockTypingOwnershipForControlExpressions@L61442`
- `rule.16.T-If@L61461`, `rule.16.T-If-No-Else@L61477`, `rule.16.CheckIfFamily@L61493`, `req.16.PatternTypingOwnershipForControlExpressions@L61506`, `rule.16.T-If-Is@L61522`, `rule.16.T-If-Is-No-Else@L61538`, `rule.16.IfCaseTypingFamily@L61554`, `rule.16.CheckIfIsAndIfCaseFamily@L61567`
- `req.16.LoopInvariantTypingOwnership@L61581`, `rule.16.T-Loop-Infinite@L61594`, `rule.16.T-Loop-Conditional@L61610`, `rule.16.T-Loop-Iter@L61626`, `rule.16.AsyncIteratorLoopTypingFamily@L61642`, `rule.16.EvalSigma-If-True@L61657`, `rule.16.EvalSigma-If-False-None@L61673`, `rule.16.EvalSigma-If-False-Some@L61689`
- `rule.16.EvalSigma-If-Ctrl@L61705`, `rule.16.EvalSigma-If-Is@L61721`, `rule.16.EvalSigma-If-Is-Ctrl@L61737`, `rule.16.EvalSigma-If-Cases@L61753`, `rule.16.EvalSigma-If-Cases-Ctrl@L61769`, `rule.16.EvalIfCasesFamily@L61785`, `rule.16.EvalSigma-Block@L61798`, `def.16.LoopIterableTypePredicates@L61814`
- `def.16.LoopIteratorRuntime@L61832`, `def.16.LoopIterJudgement@L61865`, `rule.16.EvalSigma-Loop-Infinite-Step@L61878`, `rule.16.EvalSigma-Loop-Infinite-Continue@L61894`, `rule.16.EvalSigma-Loop-Infinite-Break@L61910`, `rule.16.EvalSigma-Loop-Infinite-Ctrl@L61926`, `rule.16.EvalSigma-Loop-Cond-False@L61942`, `rule.16.EvalSigma-Loop-Cond-True-Step@L61958`
- `rule.16.EvalSigma-Loop-Cond-Continue@L61974`, `rule.16.EvalSigma-Loop-Cond-Break@L61990`, `rule.16.EvalSigma-Loop-Cond-Ctrl@L62006`, `rule.16.EvalSigma-Loop-Cond-Body-Ctrl@L62022`, `rule.16.EvalSigma-Loop-Iter@L62038`, `rule.16.EvalSigma-Loop-Iter-Ctrl@L62054`, `rule.16.LoopIter-Done@L62070`, `rule.16.LoopIter-Step-Val@L62086`
- `rule.16.LoopIter-Step-Continue@L62102`, `rule.16.LoopIter-Step-Break@L62118`, `rule.16.LoopIter-Step-Ctrl@L62134`, `rule.16.Lower-Expr-If@L62152`, `rule.16.Lower-Expr-If-Is@L62168`, `rule.16.Lower-Expr-If-Cases@L62184`, `rule.16.LowerLoopExpressionFamily@L62200`, `rule.16.Lower-Expr-Block@L62213`
- `req.16.ControlExpressionLoweringOwnership@L62229`, `diag.16.ControlExpressions@L62244`, `req.16.ControlExpressionDiagnosticOwnership@L62257`, `grammar.16.EffectfulCoreExpressions@L62275`, `req.16.RegionAliasAllocRewrite@L62295`, `rule.16.Parse-Unary-Deref@L62310`, `rule.16.Parse-Unary-AddressOf@L62326`, `rule.16.Parse-Unary-Move@L62342`
- `rule.16.Postfix-Propagate@L62358`, `rule.16.Parse-Alloc-Implicit@L62374`, `rule.16.Parse-Unsafe-Expr@L62390`, `def.16.EffectfulCoreExprAst@L62408`, `rule.16.ResolveExpr-Alloc-Explicit-ByAlias@L62421`, `def.16.AddressOfStaticHelpers@L62440`, `rule.16.T-Unsafe-Expr@L62455`, `rule.16.Chk-Unsafe-Expr@L62471`
- `rule.16.T-AddrOf@L62487`, `rule.16.T-Deref-Ptr@L62503`, `rule.16.T-Deref-Raw@L62519`, `rule.16.DerefPlaceTypingFamily@L62535`, `rule.16.T-Move@L62548`, `rule.16.T-Alloc-Explicit@L62564`, `rule.16.T-Alloc-Implicit@L62580`, `def.16.SuccessMember@L62596`
- `rule.16.T-Propagate@L62609`, `def.16.SuccessMemberAsync@L62625`, `rule.16.T-Async-Try@L62638`, `rule.16.Async-Try-Infallible-Err@L62654`, `rule.16.EvalSigma-UnsafeBlock@L62672`, `rule.16.EvalSigma-AddressOf@L62688`, `rule.16.EvalSigma-Deref@L62704`, `rule.16.EvalSigma-Move@L62720`
- `rule.16.EvalSigma-Alloc-Implicit@L62736`, `rule.16.EvalSigma-Alloc-Implicit-Ctrl@L62752`, `rule.16.EvalSigma-Alloc-Explicit@L62768`, `rule.16.EvalSigma-Alloc-Explicit-Ctrl@L62784`, `rule.16.EvalSigma-Propagate-Success@L62800`, `rule.16.EvalSigma-Propagate-Success-Async@L62816`, `rule.16.EvalSigma-Propagate-Error@L62832`, `rule.16.EvalSigma-Propagate-Error-Async@L62848`
- `rule.16.EvalSigma-Propagate-Ctrl@L62865`, `def.16.ExprStateAndTerminalExpr@L62881`, `rule.16.StepSigma-Pure@L62896`, `rule.16.StepSigma-Alloc-Implicit@L62912`, `rule.16.StepSigma-Alloc-Implicit-Ctrl@L62928`, `rule.16.StepSigma-Alloc-Explicit@L62944`, `rule.16.StepSigma-Alloc-Explicit-Ctrl@L62960`, `rule.16.StepSigma-Block@L62976`
- `rule.16.StepSigma-UnsafeBlock@L62992`, `rule.16.StepSigma-Loop@L63008`, `rule.16.StepSigma-Stateful-Other@L63024`, `rule.16.Lower-Expr-UnsafeBlock@L63042`, `rule.16.Lower-Expr-Move@L63058`, `rule.16.Lower-Expr-AddressOf@L63074`, `rule.16.Lower-Expr-Deref@L63090`, `rule.16.Lower-Expr-Alloc@L63106`
- `rule.16.Lower-Expr-Propagate-Success@L63122`, `rule.16.Lower-Expr-Propagate-Return@L63138`, `req.16.EffectfulCoreLoweringMechanics@L63154`, `diag.16.EffectfulCoreExpressions@L63169`, `grammar.16.ClosureAndPipelineExpressions@L63186`, `req.16.ClosureParamTrailingComma@L63205`, `req.16.ClosureUnionParamParentheses@L63218`, `req.16.ClosureInvocationOrdinaryCallSyntax@L63231`
- `rule.16.Parse-Pipeline@L63246`, `rule.16.Parse-PipelineTail-Stop@L63262`, `rule.16.Parse-PipelineTail-Cons@L63278`, `rule.16.Parse-Closure-Expr@L63294`, `rule.16.Parse-Closure-Expr-Empty@L63310`, `rule.16.Parse-ClosureParams-Single@L63326`, `rule.16.Parse-ClosureParams-Cons@L63342`, `rule.16.Parse-ClosureParamType-Grouped@L63358`
- `rule.16.Parse-ClosureParamType-Plain@L63374`, `rule.16.Parse-ClosureParam-MoveTyped@L63390`, `rule.16.Parse-ClosureParam-MoveUntyped@L63406`, `rule.16.Parse-ClosureParam-Typed@L63422`, `rule.16.Parse-ClosureParam-Untyped@L63438`, `rule.16.Parse-ClosureRetOpt-Some@L63454`, `rule.16.Parse-ClosureRetOpt-None@L63470`, `rule.16.Parse-ClosureBody-Block@L63486`
- `rule.16.Parse-ClosureBody-Expr@L63502`, `def.16.ClosurePipelineAstForms@L63520`, `def.16.ClosureCaptureSets@L63539`, `def.16.ClosureEscapeClassification@L63559`, `def.16.ClosureParameterAccessors@L63574`, `rule.16.T-Closure-NonCapturing@L63588`, `rule.16.T-Closure-Capturing@L63606`, `rule.16.T-Closure-Escaping@L63625`
- `rule.16.K-Closure-Escape-Type@L63645`, `rule.16.Capture-Const@L63661`, `rule.16.Capture-Shared@L63677`, `rule.16.Capture-Unique-Err@L63693`, `rule.16.T-ClosureCall@L63709`, `rule.16.Infer-Closure-Params@L63725`, `rule.16.Infer-Closure-Params-Err@L63741`, `rule.16.Infer-Closure-Return@L63757`
- `req.16.ClosureSharedDependencyInference@L63773`, `def.16.ClosureCaptureBindingAccessors@L63786`, `rule.16.B-Closure-NonCapturing@L63810`, `rule.16.B-Closure-Capturing@L63826`, `rule.16.B-Closure-MoveCapture-Moved-Err@L63845`, `rule.16.B-Closure-MoveCapture-Immovable-Err@L63862`, `rule.16.B-Closure-RefCapture-Moved-Err@L63879`, `rule.16.T-Pipeline@L63896`
- `rule.16.T-Pipeline-NotCallable-Err@L63914`, `rule.16.T-Pipeline-TypeMismatch-Err@L63931`, `rule.16.T-Pipeline-ArgCount-Err@L63949`, `rule.16.B-Pipeline@L63966`, `req.16.ClosureParamInferenceFailure@L63982`, `req.16.ClosureSharedDependencyInferenceRestated@L63995`, `def.16.ClosureEnvironmentRuntimeModel@L64010`, `rule.16.EvalSigma-Closure-NonCapturing@L64046`
- `rule.16.EvalSigma-Closure-Capturing@L64062`, `def.16.MarkMoved@L64080`, `rule.16.EvalSigma-ClosureCall@L64094`, `def.16.ClosureCallRuntimeHelpers@L64112`, `rule.16.EvalSigma-ClosureCall-Ctrl@L64134`, `rule.16.EvalSigma-ClosureCall-Ctrl-Args@L64150`, `req.16.ClosureCallResolvedInternalFormRuntime@L64167`, `req.16.PipelineDesugaring@L64180`
- `rule.16.EvalSigma-Pipeline-Func@L64193`, `rule.16.EvalSigma-Pipeline-Closure@L64210`, `rule.16.EvalSigma-Pipeline-Ctrl-Left@L64227`, `rule.16.EvalSigma-Pipeline-Ctrl-Right@L64243`, `def.16.ClosureLoweringCaptureTypes@L64261`, `rule.16.Layout-ClosureEnv@L64277`, `rule.16.Layout-ClosureEnv-Empty@L64293`, `rule.16.Lower-Expr-Closure-NonCapturing@L64309`
- `rule.16.Lower-Expr-Closure-Capturing@L64325`, `def.16.LowerCaptureEnv@L64343`, `def.16.CapturedIdentifierLoweringHelpers@L64363`, `rule.16.Lower-CapturedIdent-Ref@L64378`, `req.16.LowerCapturedIdentRefTemporaries@L64395`, `rule.16.Lower-CapturedIdent-Move@L64408`, `def.16.ClosureEnvParam@L64425`, `def.16.ClosureCodeSig@L64438`
- `rule.16.Lower-Closure-Call@L64455`, `req.16.LowerClosureCallResolvedInternalForm@L64473`, `rule.16.Lower-Expr-Pipeline@L64486`, `def.16.LowerPipelineCallablePredicates@L64504`, `diag.16.ClosureAndPipelineExpressions@L64522`, `diag.16.ExpressionDiagnosticsSupplement@L64537`

#### `spec.patterns`

Count: 161 total; 161 required; 0 recommended; 0 informative. Ledger line span: L64193-L66741.

- `grammar.17.BasicPatterns@L64578`, `rule.17.Parse-Pattern-Literal@L64595`, `rule.17.Parse-Pattern-Wildcard@L64611`, `rule.17.Parse-Pattern-Identifier@L64627`, `def.17.PatternAstForms@L64645`, `def.17.PatternJudgements@L64661`, `def.17.PermWrap@L64674`, `rule.17.Pat-StripPerm@L64689`
- `def.17.PatternNameExtractionJudgement@L64705`, `rule.17.Pat-Ident-Names@L64718`, `rule.17.Pat-Wild@L64732`, `rule.17.Pat-Lit@L64747`, `rule.17.Pat-Dup-R-Err@L64762`, `rule.17.Pat-Wildcard-R@L64780`, `rule.17.Pat-Ident-R@L64795`, `rule.17.Pat-Literal-R@L64810`
- `def.17.PatternBindingEnvironment@L64828`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64843`, `rule.17.Match-Wildcard@L64865`, `rule.17.Match-Ident@L64880`, `rule.17.Match-Literal@L64895`, `req.17.BasicPatternLoweringShared@L64913`, `diag.17.BasicPatterns@L64928`, `grammar.17.TupleRecordPatterns@L64945`
- `req.17.TuplePatternSingleElementSemicolon@L64964`, `rule.17.Parse-Pattern-Tuple@L64979`, `rule.17.Parse-Pattern-Record@L64995`, `rule.17.Parse-TuplePatternElems-Empty@L65011`, `rule.17.Parse-TuplePatternElems-Single@L65027`, `rule.17.Parse-TuplePatternElems-Many@L65043`, `rule.17.Parse-FieldPatternList-Empty@L65059`, `rule.17.Parse-FieldPatternList-Cons@L65075`
- `rule.17.Parse-FieldPattern@L65091`, `rule.17.Parse-FieldPatternTailOpt-None@L65107`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65123`, `rule.17.Parse-FieldPatternTail-End@L65139`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65155`, `rule.17.Parse-FieldPatternTail-Comma@L65171`, `def.17.FieldPatternAstAndAccessors@L65189`, `rule.17.PatNames-TuplePattern@L65206`
- `rule.17.Pat-Record-Field-Explicit@L65221`, `rule.17.Pat-Record-Field-Implicit@L65237`, `rule.17.PatNames-RecordPattern@L65252`, `rule.17.Pat-Tuple-R-Arity-Err@L65269`, `rule.17.Pat-Tuple-R@L65285`, `rule.17.Pat-Record-R@L65301`, `rule.17.RecordPattern-UnknownField@L65317`, `def.17.MatchRecordJudgement@L65335`
- `rule.17.MatchRecord-Empty@L65349`, `rule.17.MatchRecord-Cons-Implicit@L65364`, `rule.17.MatchRecord-Cons-Explicit@L65380`, `rule.17.Match-Tuple@L65396`, `rule.17.Match-Record@L65412`, `req.17.TupleRecordPatternLoweringShared@L65430`, `diag.17.TupleRecordPatterns@L65445`, `grammar.17.EnumModalPatterns@L65462`
- `req.17.EnumPayloadSingleElementTuple@L65480`, `rule.17.Parse-Pattern-Enum@L65495`, `rule.17.Parse-Pattern-Modal@L65511`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65527`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65543`, `rule.17.Parse-EnumPayloadPatternElems-One@L65559`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65575`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65591`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65607`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65623`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65639`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65655`, `def.17.EnumModalPayloadPatterns@L65673`, `rule.17.Pat-Enum-None@L65687`, `rule.17.Pat-Enum-Tuple@L65702`, `rule.17.Pat-Enum-Record@L65718`
- `rule.17.Pat-Modal-None@L65734`, `rule.17.Pat-Modal-Record@L65749`, `rule.17.Pat-Enum-Unit-R@L65767`, `rule.17.Pat-Enum-Tuple-R@L65783`, `rule.17.Pat-Enum-Record-R@L65799`, `rule.17.Pat-Modal-R@L65815`, `rule.17.Pat-Modal-State-R@L65831`, `def.17.MatchModalJudgement@L65849`
- `rule.17.Match-Modal-Empty@L65862`, `rule.17.Match-Modal-Record@L65877`, `rule.17.Match-Enum-Unit@L65893`, `rule.17.Match-Enum-Tuple@L65909`, `rule.17.Match-Enum-Record@L65925`, `rule.17.Match-Modal-General@L65941`, `rule.17.Match-Modal-State@L65957`, `req.17.EnumModalPatternLoweringShared@L65975`
- `diag.17.EnumModalPatterns@L65990`, `grammar.17.RangePatterns@L66007`, `rule.17.Parse-Pattern@L66024`, `rule.17.Parse-Pattern-Err@L66040`, `rule.17.Parse-Pattern-Range-None@L66056`, `rule.17.Parse-Pattern-Range@L66072`, `def.17.RangePatternAst@L66090`, `rule.17.Pat-Range-R@L66106`
- `rule.17.RangePattern-NonConst@L66122`, `rule.17.RangePattern-Empty@L66138`, `def.17.ConstPat@L66156`, `rule.17.Match-Range-Inclusive@L66169`, `rule.17.Match-Range-Exclusive@L66185`, `req.17.RangePatternLoweringShared@L66202`, `diag.17.RangePatterns@L66217`, `grammar.17.CaseClauses@L66234`
- `def.17.CaseClauseParsingGroup@L66252`, `rule.17.Parse-IfCases-Cons@L66265`, `rule.17.Parse-IfCase@L66281`, `rule.17.Parse-IfCasesTail-End@L66297`, `rule.17.Parse-IfCasesTail-Else@L66313`, `rule.17.Parse-IfCasesTail-Cons@L66329`, `def.17.IfCaseAst@L66347`, `def.17.BindOrder@L66360`
- `req.17.CaseBodyTypingScope@L66375`, `def.17.IfCaseEvaluationJudgements@L66390`, `rule.17.EvalIfCase-Fail@L66404`, `rule.17.EvalIfCase-Hit@L66420`, `rule.17.EvalIfCases-Head@L66436`, `rule.17.EvalIfCases-Tail@L66452`, `rule.17.EvalIfCases-Else@L66468`, `rule.17.EvalIfCases-None@L66484`
- `def.17.PatternLoweringJudgements@L66502`, `rule.17.Lower-Pat-Correctness@L66516`, `def.17.IfCaseValueCorrect@L66532`, `rule.17.Lower-IfCases-Correctness@L66545`, `def.17.PatternTagHelpers@L66561`, `rule.17.TagOf-Enum@L66577`, `rule.17.TagOf-Modal@L66593`, `rule.17.Lower-BindList-Empty@L66609`
- `rule.17.Lower-BindList-Cons@L66624`, `rule.17.Lower-Pat-General@L66640`, `rule.17.Lower-Pat-Err@L66656`, `rule.17.Lower-IfCases@L66672`, `diag.17.CaseClauses@L66690`, `req.17.ExhaustivenessNoSyntax@L66707`, `req.17.ExhaustivenessNotParserOwned@L66722`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66737`
- `def.17.EnumCaseCoverageHelpers@L66759`, `def.17.ModalCaseCoverageHelpers@L66773`, `def.17.UnionCaseCoverageHelpers@L66786`, `def.17.EnumCaseAnalysisGroup@L66803`, `rule.17.T-IfCase-Enum@L66816`, `def.17.ModalCaseAnalysisGroup@L66832`, `rule.17.T-IfCase-Modal@L66845`, `rule.17.IfCase-Modal-NonExhaustive@L66861`
- `def.17.UnionCaseAnalysisGroup@L66877`, `rule.17.T-IfCase-Union@L66890`, `rule.17.IfCase-Union-NonExhaustive@L66906`, `rule.17.Chk-IfCase-Union@L66922`, `def.17.OtherCaseAnalysisGroup@L66938`, `rule.17.T-IfCase-Other@L66951`, `rule.17.Chk-IfCase-Enum@L66967`, `rule.17.IfCase-Enum-NonExhaustive@L66983`
- `rule.17.Chk-IfCase-Modal@L66999`, `rule.17.Chk-IfCase-Other@L67015`, `rule.17.Chk-IfIs@L67031`, `rule.17.Chk-IfIs-No-Else@L67047`, `rule.17.IfCase-Unreachable@L67063`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L67081`, `req.17.ExhaustivenessNoAdditionalLowering@L67096`, `diag.17.ExhaustivenessAndReachability@L67111`
- `diag.17.PatternDiagnosticsSupplement@L67126`
- `grammar.17.BasicPatterns@L64578`, `rule.17.Parse-Pattern-Literal@L64595`, `rule.17.Parse-Pattern-Wildcard@L64611`, `rule.17.Parse-Pattern-Identifier@L64627`, `def.17.PatternAstForms@L64645`, `def.17.PatternJudgements@L64661`, `def.17.PermWrap@L64674`, `rule.17.Pat-StripPerm@L64689`
- `def.17.PatternNameExtractionJudgement@L64705`, `rule.17.Pat-Ident-Names@L64718`, `rule.17.Pat-Wild@L64732`, `rule.17.Pat-Lit@L64747`, `rule.17.Pat-Dup-R-Err@L64762`, `rule.17.Pat-Wildcard-R@L64780`, `rule.17.Pat-Ident-R@L64795`, `rule.17.Pat-Literal-R@L64810`
- `def.17.PatternBindingEnvironment@L64828`, `def.17.PatternMatchingJudgementAndLiteralTypes@L64843`, `rule.17.Match-Wildcard@L64865`, `rule.17.Match-Ident@L64880`, `rule.17.Match-Literal@L64895`, `req.17.BasicPatternLoweringShared@L64913`, `diag.17.BasicPatterns@L64928`, `grammar.17.TupleRecordPatterns@L64945`
- `req.17.TuplePatternSingleElementSemicolon@L64964`, `rule.17.Parse-Pattern-Tuple@L64979`, `rule.17.Parse-Pattern-Record@L64995`, `rule.17.Parse-TuplePatternElems-Empty@L65011`, `rule.17.Parse-TuplePatternElems-Single@L65027`, `rule.17.Parse-TuplePatternElems-Many@L65043`, `rule.17.Parse-FieldPatternList-Empty@L65059`, `rule.17.Parse-FieldPatternList-Cons@L65075`
- `rule.17.Parse-FieldPattern@L65091`, `rule.17.Parse-FieldPatternTailOpt-None@L65107`, `rule.17.Parse-FieldPatternTailOpt-Yes@L65123`, `rule.17.Parse-FieldPatternTail-End@L65139`, `rule.17.Parse-FieldPatternTail-TrailingComma@L65155`, `rule.17.Parse-FieldPatternTail-Comma@L65171`, `def.17.FieldPatternAstAndAccessors@L65189`, `rule.17.PatNames-TuplePattern@L65206`
- `rule.17.Pat-Record-Field-Explicit@L65221`, `rule.17.Pat-Record-Field-Implicit@L65237`, `rule.17.PatNames-RecordPattern@L65252`, `rule.17.Pat-Tuple-R-Arity-Err@L65269`, `rule.17.Pat-Tuple-R@L65285`, `rule.17.Pat-Record-R@L65301`, `rule.17.RecordPattern-UnknownField@L65317`, `def.17.MatchRecordJudgement@L65335`
- `rule.17.MatchRecord-Empty@L65349`, `rule.17.MatchRecord-Cons-Implicit@L65364`, `rule.17.MatchRecord-Cons-Explicit@L65380`, `rule.17.Match-Tuple@L65396`, `rule.17.Match-Record@L65412`, `req.17.TupleRecordPatternLoweringShared@L65430`, `diag.17.TupleRecordPatterns@L65445`, `grammar.17.EnumModalPatterns@L65462`
- `req.17.EnumPayloadSingleElementTuple@L65480`, `rule.17.Parse-Pattern-Enum@L65495`, `rule.17.Parse-Pattern-Modal@L65511`, `rule.17.Parse-EnumPatternPayloadOpt-None@L65527`, `rule.17.Parse-EnumPayloadPatternElems-Empty@L65543`, `rule.17.Parse-EnumPayloadPatternElems-One@L65559`, `rule.17.Parse-EnumPayloadPatternElems-TrailingComma@L65575`, `rule.17.Parse-EnumPayloadPatternElems-Many@L65591`
- `rule.17.Parse-EnumPatternPayloadOpt-Tuple@L65607`, `rule.17.Parse-EnumPatternPayloadOpt-Record@L65623`, `rule.17.Parse-ModalPatternPayloadOpt-None@L65639`, `rule.17.Parse-ModalPatternPayloadOpt-Record@L65655`, `def.17.EnumModalPayloadPatterns@L65673`, `rule.17.Pat-Enum-None@L65687`, `rule.17.Pat-Enum-Tuple@L65702`, `rule.17.Pat-Enum-Record@L65718`
- `rule.17.Pat-Modal-None@L65734`, `rule.17.Pat-Modal-Record@L65749`, `rule.17.Pat-Enum-Unit-R@L65767`, `rule.17.Pat-Enum-Tuple-R@L65783`, `rule.17.Pat-Enum-Record-R@L65799`, `rule.17.Pat-Modal-R@L65815`, `rule.17.Pat-Modal-State-R@L65831`, `def.17.MatchModalJudgement@L65849`
- `rule.17.Match-Modal-Empty@L65862`, `rule.17.Match-Modal-Record@L65877`, `rule.17.Match-Enum-Unit@L65893`, `rule.17.Match-Enum-Tuple@L65909`, `rule.17.Match-Enum-Record@L65925`, `rule.17.Match-Modal-General@L65941`, `rule.17.Match-Modal-State@L65957`, `req.17.EnumModalPatternLoweringShared@L65975`
- `diag.17.EnumModalPatterns@L65990`, `grammar.17.RangePatterns@L66007`, `rule.17.Parse-Pattern@L66024`, `rule.17.Parse-Pattern-Err@L66040`, `rule.17.Parse-Pattern-Range-None@L66056`, `rule.17.Parse-Pattern-Range@L66072`, `def.17.RangePatternAst@L66090`, `rule.17.Pat-Range-R@L66106`
- `rule.17.RangePattern-NonConst@L66122`, `rule.17.RangePattern-Empty@L66138`, `def.17.ConstPat@L66156`, `rule.17.Match-Range-Inclusive@L66169`, `rule.17.Match-Range-Exclusive@L66185`, `req.17.RangePatternLoweringShared@L66202`, `diag.17.RangePatterns@L66217`, `grammar.17.CaseClauses@L66234`
- `def.17.CaseClauseParsingGroup@L66252`, `rule.17.Parse-IfCases-Cons@L66265`, `rule.17.Parse-IfCase@L66281`, `rule.17.Parse-IfCasesTail-End@L66297`, `rule.17.Parse-IfCasesTail-Else@L66313`, `rule.17.Parse-IfCasesTail-Cons@L66329`, `def.17.IfCaseAst@L66347`, `def.17.BindOrder@L66360`
- `req.17.CaseBodyTypingScope@L66375`, `def.17.IfCaseEvaluationJudgements@L66390`, `rule.17.EvalIfCase-Fail@L66404`, `rule.17.EvalIfCase-Hit@L66420`, `rule.17.EvalIfCases-Head@L66436`, `rule.17.EvalIfCases-Tail@L66452`, `rule.17.EvalIfCases-Else@L66468`, `rule.17.EvalIfCases-None@L66484`
- `def.17.PatternLoweringJudgements@L66502`, `rule.17.Lower-Pat-Correctness@L66516`, `def.17.IfCaseValueCorrect@L66532`, `rule.17.Lower-IfCases-Correctness@L66545`, `def.17.PatternTagHelpers@L66561`, `rule.17.TagOf-Enum@L66577`, `rule.17.TagOf-Modal@L66593`, `rule.17.Lower-BindList-Empty@L66609`
- `rule.17.Lower-BindList-Cons@L66624`, `rule.17.Lower-Pat-General@L66640`, `rule.17.Lower-Pat-Err@L66656`, `rule.17.Lower-IfCases@L66672`, `diag.17.CaseClauses@L66690`, `req.17.ExhaustivenessNoSyntax@L66707`, `req.17.ExhaustivenessNotParserOwned@L66722`, `def.17.ExhaustivenessIrrefutabilityHelpers@L66737`
- `def.17.EnumCaseCoverageHelpers@L66759`, `def.17.ModalCaseCoverageHelpers@L66773`, `def.17.UnionCaseCoverageHelpers@L66786`, `def.17.EnumCaseAnalysisGroup@L66803`, `rule.17.T-IfCase-Enum@L66816`, `def.17.ModalCaseAnalysisGroup@L66832`, `rule.17.T-IfCase-Modal@L66845`, `rule.17.IfCase-Modal-NonExhaustive@L66861`
- `def.17.UnionCaseAnalysisGroup@L66877`, `rule.17.T-IfCase-Union@L66890`, `rule.17.IfCase-Union-NonExhaustive@L66906`, `rule.17.Chk-IfCase-Union@L66922`, `def.17.OtherCaseAnalysisGroup@L66938`, `rule.17.T-IfCase-Other@L66951`, `rule.17.Chk-IfCase-Enum@L66967`, `rule.17.IfCase-Enum-NonExhaustive@L66983`
- `rule.17.Chk-IfCase-Modal@L66999`, `rule.17.Chk-IfCase-Other@L67015`, `rule.17.Chk-IfIs@L67031`, `rule.17.Chk-IfIs-No-Else@L67047`, `rule.17.IfCase-Unreachable@L67063`, `req.17.ExhaustivenessNoAdditionalDynamicSemantics@L67081`, `req.17.ExhaustivenessNoAdditionalLowering@L67096`, `diag.17.ExhaustivenessAndReachability@L67111`
- `diag.17.PatternDiagnosticsSupplement@L67126`

#### `spec.statements`

Count: 260 total; 260 required; 0 recommended; 0 informative. Ledger line span: L66771-L70869.

- `grammar.18.Blocks@L67156`, `req.18.BlockStatementExternalDefinitions@L67186`, `def.18.StatementTerminators@L67202`, `def.18.AttachStmtAttrs@L67217`, `rule.18.Parse-Statement@L67230`, `rule.18.Parse-Statement-Err@L67246`, `rule.18.Parse-Block@L67262`, `def.18.RequiredStatementTerminators@L67278`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67291`, `rule.18.ConsumeTerminatorOpt-Req-No@L67307`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67323`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67339`, `def.18.SkipNL@L67355`, `rule.18.ParseStmtSeq-End@L67369`, `rule.18.ParseStmtSeq-TailExpr@L67385`, `rule.18.ParseStmtSeq-Cons@L67401`
- `def.18.SyncStmt@L67417`, `def.18.StatementAstForms@L67432`, `def.18.LastStmtAndResultType@L67445`, `def.18.BindingEnvironmentHelpers@L67464`, `def.18.StatementTypingJudgements@L67481`, `def.18.LoopFlag@L67494`, `def.18.ScopeStackTypeHelpers@L67507`, `rule.18.T-ErrorStmt@L67521`
- `rule.18.BlockInfo-Res@L67536`, `rule.18.BlockInfo-Res-Err@L67552`, `rule.18.BlockInfo-Tail@L67568`, `rule.18.BlockInfo-ReturnTail@L67584`, `rule.18.BlockInfo-Unit@L67600`, `rule.18.T-Block@L67616`, `req.18.BlockCheckingModeValidation@L67632`, `req.18.BlockExprExpressionFormOwnership@L67645`
- `def.18.StatementExecutionJudgements@L67660`, `def.18.ControlAndStatementOutcomes@L67673`, `def.18.BlockExitOutcome@L67692`, `def.18.BlockExit@L67708`, `def.18.EvalBlockBodySigma@L67721`, `def.18.EvalBlockSigma@L67739`, `def.18.EvalBlockBindSigma@L67752`, `def.18.EvalInScopeSigma@L67765`
- `def.18.PlaceEvaluationHelpersGroup@L67778`, `def.18.PlaceJudgements@L67791`, `rule.18.ExecSeq-Empty@L67805`, `rule.18.ExecSeq-Cons-Ok@L67820`, `rule.18.ExecSeq-Cons-Ctrl@L67836`, `rule.18.ExecSigma-Error@L67852`, `def.18.ExecState@L67867`, `rule.18.Step-Exec-Other-Ok@L67880`
- `rule.18.Step-Exec-Other-Ctrl@L67896`, `rule.18.Step-ExecSeq-Ok@L67912`, `rule.18.Step-ExecSeq-Ctrl@L67928`, `rule.18.Step-Exec-Defer@L67944`, `req.18.BlockExprEvalDelegatesToBlock@L67960`, `def.18.LowerStatementJudgements@L67975`, `rule.18.Lower-Stmt-Correctness@L67988`, `rule.18.Lower-Block-Correctness@L68004`
- `def.18.StatementLoweringTotality@L68020`, `rule.18.Lower-StmtList-Empty@L68034`, `rule.18.Lower-StmtList-Cons@L68049`, `rule.18.Lower-Block-Tail@L68065`, `rule.18.Lower-Block-Unit@L68081`, `rule.18.Lower-Stmt-Error@L68097`, `req.18.TemporaryCleanupLowering@L68112`, `def.18.BlockLoopLoweringTotality@L68144`
- `rule.18.Lower-Loop-Infinite@L68160`, `rule.18.Lower-Loop-Cond@L68176`, `rule.18.Lower-Loop-Iter@L68192`, `diag.18.Blocks@L68210`, `grammar.18.BindingStatements@L68227`, `rule.18.Parse-Binding-Stmt@L68245`, `rule.18.Parse-BindingAfterLetVar@L68261`, `rule.18.LetOrVarStmt-Let@L68277`
- `rule.18.LetOrVarStmt-Var@L68293`, `def.18.LetOrVarStmtAst@L68311`, `def.18.BindingAstAndAccessors@L68324`, `def.18.IntroEnt@L68347`, `rule.18.IntroAll-Empty@L68360`, `rule.18.IntroAll-Cons@L68375`, `rule.18.IntroAllVar-Empty@L68391`, `rule.18.IntroAllVar-Cons@L68406`
- `rule.18.T-LetStmt-Ann@L68422`, `rule.18.T-LetStmt-Ann-Mismatch@L68438`, `rule.18.T-LetStmt-Infer@L68454`, `rule.18.T-LetStmt-Infer-Err@L68470`, `req.18.VarStmtTypingMirrorsLet@L68486`, `rule.18.Let-Refutable-Pattern-Err@L68499`, `rule.18.B-LetVar-UniqueNonMove-Err@L68515`, `def.18.SuspendUniqueBind@L68531`
- `rule.18.B-LetVar@L68546`, `rule.18.Prov-LetVar-Ordinary@L68562`, `rule.18.Prov-LetVar-Region-Alias@L68578`, `rule.18.Prov-LetVar-Region-Fresh@L68594`, `def.18.BindVal@L68612`, `def.18.BindPatternRuntimeHelpers@L68625`, `rule.18.BindList-Empty@L68639`, `rule.18.BindList-Cons@L68654`
- `def.18.BindPattern@L68670`, `rule.18.ExecSigma-Let@L68683`, `rule.18.ExecSigma-Let-Ctrl@L68699`, `req.18.VarExecutionMirrorsLet@L68715`, `rule.18.Lower-Stmt-Let@L68730`, `rule.18.Lower-Stmt-Var@L68746`, `diag.18.BindingStatements@L68764`, `grammar.18.LocalUsingStatements@L68781`
- `rule.18.Parse-UsingLocal-Stmt@L68798`, `def.18.UsingLocalStmtAst@L68816`, `req.18.UsingLocalUsesUsingAlias@L68831`, `rule.18.T-UsingLocalStmt@L68844`, `rule.18.T-UsingLocalStmt-Err@L68860`, `req.18.UsingLocalAliasIdentity@L68876`, `rule.18.ExecSigma-UsingLocal@L68891`, `req.18.UsingLocalNoRuntimeEffect@L68906`
- `rule.18.Lower-Stmt-UsingLocal@L68921`, `req.18.UsingLocalNoRuntimeIR@L68936`, `diag.18.LocalUsingStatements@L68951`, `grammar.18.AssignmentStatements@L68968`, `rule.18.Parse-Assign-Stmt@L68986`, `rule.18.AssignOrCompound-Assign@L69002`, `rule.18.AssignOrCompound-Compound@L69018`, `def.18.AssignmentAstForms@L69036`
- `def.18.PlaceRoot@L69050`, `rule.18.T-Assign@L69069`, `rule.18.T-CompoundAssign@L69085`, `rule.18.Assign-NotPlace@L69101`, `rule.18.Assign-Immutable-Err@L69117`, `rule.18.Assign-Type-Err@L69133`, `rule.18.Assign-Const-Err@L69149`, `req.18.AssignmentBindingStateRules@L69165`
- `req.18.AssignmentProvenanceRules@L69178`, `req.18.AssignmentProvenanceEscapeFailures@L69191`, `def.18.AssignmentRootBinding@L69206`, `def.18.DropOnAssign@L69221`, `def.18.DropSubvalueJudgement@L69238`, `rule.18.DropSubvalue-Do@L69251`, `rule.18.DropSubvalue-Skip@L69267`, `rule.18.ExecSigma-Assign@L69283`
- `rule.18.ExecSigma-Assign-Ctrl@L69299`, `rule.18.ExecSigma-CompoundAssign@L69315`, `req.18.CompoundAssignControlPropagation@L69331`, `rule.18.Lower-Stmt-Assign@L69346`, `rule.18.Lower-Stmt-CompoundAssign@L69362`, `diag.18.AssignmentStatements@L69380`, `grammar.18.ExpressionStatements@L69397`, `rule.18.Parse-Expr-Stmt@L69414`
- `def.18.ExprStmtAst@L69432`, `rule.18.T-ExprStmt@L69447`, `req.18.ExprStmtStateAndProvenanceRules@L69463`, `rule.18.ExecSigma-ExprStmt@L69478`, `rule.18.Lower-Stmt-Expr@L69496`, `diag.18.ExpressionStatements@L69514`, `grammar.18.Defer@L69531`, `rule.18.Parse-Defer-Stmt@L69548`
- `def.18.DeferStmtAst@L69566`, `rule.18.T-DeferStmt@L69581`, `rule.18.Defer-NonUnit-Err@L69597`, `rule.18.Defer-NonLocal-Err@L69613`, `rule.18.HasNonLocalCtrl-Return@L69629`, `rule.18.HasNonLocalCtrl-Break@L69644`, `rule.18.HasNonLocalCtrl-Continue@L69660`, `req.18.HasNonLocalCtrlPropagation@L69676`
- `def.18.DeferSafe@L69689`, `req.18.DeferStateAndProvenancePreservation@L69702`, `rule.18.ExecSigma-Defer@L69717`, `req.18.DeferCleanupSmallStep@L69733`, `req.18.DeferCleanupBigStep@L69746`, `rule.18.Lower-Stmt-Defer@L69761`, `diag.18.Defer@L69778`, `grammar.18.Region@L69795`
- `rule.18.Parse-Region-Opts-None@L69814`, `rule.18.Parse-Region-Opts-Some@L69830`, `rule.18.Parse-Region-Alias-None@L69846`, `rule.18.Parse-Region-Alias-Some@L69862`, `rule.18.Parse-Region-Stmt@L69878`, `def.18.RegionStmtAst@L69896`, `def.18.RegionTypeAndFreshNameHelpers@L69909`, `def.18.RegionOptsExpr@L69923`
- `def.18.RegionBind@L69939`, `rule.18.T-RegionStmt@L69954`, `req.18.AnonymousRegionSyntheticBinding@L69970`, `req.18.RegionBindingState@L69983`, `req.18.RegionProvenance@L69996`, `def.18.BindRegionAlias@L70011`, `rule.18.ExecSigma-Region@L70025`, `rule.18.ExecSigma-Region-Ctrl@L70041`
- `def.18.RegionRelease@L70057`, `rule.18.Step-Exec-Region-Enter@L70070`, `rule.18.Step-Exec-Region-Enter-Ctrl@L70086`, `rule.18.Step-Exec-Region-Body@L70102`, `rule.18.Step-Exec-Region-Exit-Ok@L70118`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70134`, `rule.18.Lower-Stmt-Region@L70152`, `diag.18.Region@L70170`
- `grammar.18.Frame@L70187`, `rule.18.Parse-Frame-Stmt@L70204`, `rule.18.Parse-Frame-Explicit@L70220`, `def.18.FrameStmtAst@L70238`, `def.18.InnermostActiveRegion@L70251`, `def.18.FrameBind@L70267`, `rule.18.T-FrameStmt-Implicit@L70284`, `rule.18.T-FrameStmt-Explicit@L70300`
- `rule.18.Frame-NoActiveRegion-Err@L70316`, `rule.18.Frame-Target-NotActive-Err@L70332`, `req.18.FrameSyntheticRegionBinding@L70348`, `req.18.FrameBindingState@L70361`, `req.18.FrameProvenance@L70374`, `def.18.FrameTargetResolution@L70389`, `def.18.FrameEnter@L70403`, `rule.18.ExecSigma-Frame-Implicit@L70416`
- `rule.18.ExecSigma-Frame-Explicit@L70432`, `def.18.FrameReset@L70448`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70461`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70477`, `rule.18.Step-Exec-Frame-Body@L70493`, `rule.18.Step-Exec-Frame-Exit-Ok@L70509`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70525`, `rule.18.Lower-Stmt-Frame-Implicit@L70543`
- `rule.18.Lower-Stmt-Frame-Explicit@L70559`, `diag.18.Frame@L70577`, `grammar.18.ControlTransferStatements@L70594`, `rule.18.Parse-Return-Stmt@L70613`, `rule.18.Parse-Break-Stmt@L70629`, `rule.18.Parse-Continue-Stmt@L70645`, `def.18.ControlTransferAstForms@L70663`, `rule.18.T-Return-Value@L70680`
- `rule.18.T-Return-Unit@L70696`, `rule.18.Return-Async-Type-Err@L70712`, `rule.18.Return-Async-Unit-Err@L70728`, `rule.18.Return-Type-Err@L70744`, `rule.18.Return-Unit-Err@L70760`, `rule.18.T-Break-Value@L70776`, `rule.18.T-Break-Unit@L70792`, `rule.18.Break-Outside-Loop@L70808`
- `rule.18.T-Continue@L70824`, `rule.18.Continue-Outside-Loop@L70840`, `req.18.ControlTransferBindingState@L70856`, `req.18.ControlTransferProvenance@L70869`, `rule.18.ExecSigma-Return@L70884`, `rule.18.ExecSigma-Return-Unit@L70900`, `rule.18.ExecSigma-Return-Ctrl@L70915`, `rule.18.ExecSigma-Break@L70931`
- `rule.18.ExecSigma-Break-Unit@L70947`, `rule.18.ExecSigma-Break-Ctrl@L70962`, `rule.18.ExecSigma-Continue@L70978`, `rule.18.Lower-Stmt-Return@L70995`, `rule.18.Lower-Stmt-Return-Unit@L71011`, `rule.18.Lower-Stmt-Break@L71026`, `rule.18.Lower-Stmt-Break-Unit@L71042`, `rule.18.Lower-Stmt-Continue@L71057`
- `req.18.ControlTransferTemporaryCleanupLowering@L71072`, `diag.18.ControlTransferStatements@L71092`, `grammar.18.UnsafeStatements@L71109`, `rule.18.Parse-Unsafe-Block@L71126`, `def.18.UnsafeBlockStmtAst@L71144`, `rule.18.T-UnsafeStmt@L71159`, `req.18.UnsafeStatementStateAndProvenance@L71175`, `diag.18.UnsafeRequiredOperationOwnership@L71188`
- `rule.18.ExecSigma-UnsafeStmt@L71203`, `rule.18.Lower-Stmt-UnsafeBlock@L71221`, `diag.18.UnsafeStatements@L71239`, `diag.18.StatementDiagnosticsSupplement@L71254`
- `grammar.18.Blocks@L67156`, `req.18.BlockStatementExternalDefinitions@L67186`, `def.18.StatementTerminators@L67202`, `def.18.AttachStmtAttrs@L67217`, `rule.18.Parse-Statement@L67230`, `rule.18.Parse-Statement-Err@L67246`, `rule.18.Parse-Block@L67262`, `def.18.RequiredStatementTerminators@L67278`
- `rule.18.ConsumeTerminatorOpt-Req-Yes@L67291`, `rule.18.ConsumeTerminatorOpt-Req-No@L67307`, `rule.18.ConsumeTerminatorOpt-Opt-Yes@L67323`, `rule.18.ConsumeTerminatorOpt-Opt-No@L67339`, `def.18.SkipNL@L67355`, `rule.18.ParseStmtSeq-End@L67369`, `rule.18.ParseStmtSeq-TailExpr@L67385`, `rule.18.ParseStmtSeq-Cons@L67401`
- `def.18.SyncStmt@L67417`, `def.18.StatementAstForms@L67432`, `def.18.LastStmtAndResultType@L67445`, `def.18.BindingEnvironmentHelpers@L67464`, `def.18.StatementTypingJudgements@L67481`, `def.18.LoopFlag@L67494`, `def.18.ScopeStackTypeHelpers@L67507`, `rule.18.T-ErrorStmt@L67521`
- `rule.18.BlockInfo-Res@L67536`, `rule.18.BlockInfo-Res-Err@L67552`, `rule.18.BlockInfo-Tail@L67568`, `rule.18.BlockInfo-ReturnTail@L67584`, `rule.18.BlockInfo-Unit@L67600`, `rule.18.T-Block@L67616`, `req.18.BlockCheckingModeValidation@L67632`, `req.18.BlockExprExpressionFormOwnership@L67645`
- `def.18.StatementExecutionJudgements@L67660`, `def.18.ControlAndStatementOutcomes@L67673`, `def.18.BlockExitOutcome@L67692`, `def.18.BlockExit@L67708`, `def.18.EvalBlockBodySigma@L67721`, `def.18.EvalBlockSigma@L67739`, `def.18.EvalBlockBindSigma@L67752`, `def.18.EvalInScopeSigma@L67765`
- `def.18.PlaceEvaluationHelpersGroup@L67778`, `def.18.PlaceJudgements@L67791`, `rule.18.ExecSeq-Empty@L67805`, `rule.18.ExecSeq-Cons-Ok@L67820`, `rule.18.ExecSeq-Cons-Ctrl@L67836`, `rule.18.ExecSigma-Error@L67852`, `def.18.ExecState@L67867`, `rule.18.Step-Exec-Other-Ok@L67880`
- `rule.18.Step-Exec-Other-Ctrl@L67896`, `rule.18.Step-ExecSeq-Ok@L67912`, `rule.18.Step-ExecSeq-Ctrl@L67928`, `rule.18.Step-Exec-Defer@L67944`, `req.18.BlockExprEvalDelegatesToBlock@L67960`, `def.18.LowerStatementJudgements@L67975`, `rule.18.Lower-Stmt-Correctness@L67988`, `rule.18.Lower-Block-Correctness@L68004`
- `def.18.StatementLoweringTotality@L68020`, `rule.18.Lower-StmtList-Empty@L68034`, `rule.18.Lower-StmtList-Cons@L68049`, `rule.18.Lower-Block-Tail@L68065`, `rule.18.Lower-Block-Unit@L68081`, `rule.18.Lower-Stmt-Error@L68097`, `req.18.TemporaryCleanupLowering@L68112`, `def.18.BlockLoopLoweringTotality@L68144`
- `rule.18.Lower-Loop-Infinite@L68160`, `rule.18.Lower-Loop-Cond@L68176`, `rule.18.Lower-Loop-Iter@L68192`, `diag.18.Blocks@L68210`, `grammar.18.BindingStatements@L68227`, `rule.18.Parse-Binding-Stmt@L68245`, `rule.18.Parse-BindingAfterLetVar@L68261`, `rule.18.LetOrVarStmt-Let@L68277`
- `rule.18.LetOrVarStmt-Var@L68293`, `def.18.LetOrVarStmtAst@L68311`, `def.18.BindingAstAndAccessors@L68324`, `def.18.IntroEnt@L68347`, `rule.18.IntroAll-Empty@L68360`, `rule.18.IntroAll-Cons@L68375`, `rule.18.IntroAllVar-Empty@L68391`, `rule.18.IntroAllVar-Cons@L68406`
- `rule.18.T-LetStmt-Ann@L68422`, `rule.18.T-LetStmt-Ann-Mismatch@L68438`, `rule.18.T-LetStmt-Infer@L68454`, `rule.18.T-LetStmt-Infer-Err@L68470`, `req.18.VarStmtTypingMirrorsLet@L68486`, `rule.18.Let-Refutable-Pattern-Err@L68499`, `rule.18.B-LetVar-UniqueNonMove-Err@L68515`, `def.18.SuspendUniqueBind@L68531`
- `rule.18.B-LetVar@L68546`, `rule.18.Prov-LetVar-Ordinary@L68562`, `rule.18.Prov-LetVar-Region-Alias@L68578`, `rule.18.Prov-LetVar-Region-Fresh@L68594`, `def.18.BindVal@L68612`, `def.18.BindPatternRuntimeHelpers@L68625`, `rule.18.BindList-Empty@L68639`, `rule.18.BindList-Cons@L68654`
- `def.18.BindPattern@L68670`, `rule.18.ExecSigma-Let@L68683`, `rule.18.ExecSigma-Let-Ctrl@L68699`, `req.18.VarExecutionMirrorsLet@L68715`, `rule.18.Lower-Stmt-Let@L68730`, `rule.18.Lower-Stmt-Var@L68746`, `diag.18.BindingStatements@L68764`, `grammar.18.LocalUsingStatements@L68781`
- `rule.18.Parse-UsingLocal-Stmt@L68798`, `def.18.UsingLocalStmtAst@L68816`, `req.18.UsingLocalUsesUsingAlias@L68831`, `rule.18.T-UsingLocalStmt@L68844`, `rule.18.T-UsingLocalStmt-Err@L68860`, `req.18.UsingLocalAliasIdentity@L68876`, `rule.18.ExecSigma-UsingLocal@L68891`, `req.18.UsingLocalNoRuntimeEffect@L68906`
- `rule.18.Lower-Stmt-UsingLocal@L68921`, `req.18.UsingLocalNoRuntimeIR@L68936`, `diag.18.LocalUsingStatements@L68951`, `grammar.18.AssignmentStatements@L68968`, `rule.18.Parse-Assign-Stmt@L68986`, `rule.18.AssignOrCompound-Assign@L69002`, `rule.18.AssignOrCompound-Compound@L69018`, `def.18.AssignmentAstForms@L69036`
- `def.18.PlaceRoot@L69050`, `rule.18.T-Assign@L69069`, `rule.18.T-CompoundAssign@L69085`, `rule.18.Assign-NotPlace@L69101`, `rule.18.Assign-Immutable-Err@L69117`, `rule.18.Assign-Type-Err@L69133`, `rule.18.Assign-Const-Err@L69149`, `req.18.AssignmentBindingStateRules@L69165`
- `req.18.AssignmentProvenanceRules@L69178`, `req.18.AssignmentProvenanceEscapeFailures@L69191`, `def.18.AssignmentRootBinding@L69206`, `def.18.DropOnAssign@L69221`, `def.18.DropSubvalueJudgement@L69238`, `rule.18.DropSubvalue-Do@L69251`, `rule.18.DropSubvalue-Skip@L69267`, `rule.18.ExecSigma-Assign@L69283`
- `rule.18.ExecSigma-Assign-Ctrl@L69299`, `rule.18.ExecSigma-CompoundAssign@L69315`, `req.18.CompoundAssignControlPropagation@L69331`, `rule.18.Lower-Stmt-Assign@L69346`, `rule.18.Lower-Stmt-CompoundAssign@L69362`, `diag.18.AssignmentStatements@L69380`, `grammar.18.ExpressionStatements@L69397`, `rule.18.Parse-Expr-Stmt@L69414`
- `def.18.ExprStmtAst@L69432`, `rule.18.T-ExprStmt@L69447`, `req.18.ExprStmtStateAndProvenanceRules@L69463`, `rule.18.ExecSigma-ExprStmt@L69478`, `rule.18.Lower-Stmt-Expr@L69496`, `diag.18.ExpressionStatements@L69514`, `grammar.18.Defer@L69531`, `rule.18.Parse-Defer-Stmt@L69548`
- `def.18.DeferStmtAst@L69566`, `rule.18.T-DeferStmt@L69581`, `rule.18.Defer-NonUnit-Err@L69597`, `rule.18.Defer-NonLocal-Err@L69613`, `rule.18.HasNonLocalCtrl-Return@L69629`, `rule.18.HasNonLocalCtrl-Break@L69644`, `rule.18.HasNonLocalCtrl-Continue@L69660`, `req.18.HasNonLocalCtrlPropagation@L69676`
- `def.18.DeferSafe@L69689`, `req.18.DeferStateAndProvenancePreservation@L69702`, `rule.18.ExecSigma-Defer@L69717`, `req.18.DeferCleanupSmallStep@L69733`, `req.18.DeferCleanupBigStep@L69746`, `rule.18.Lower-Stmt-Defer@L69761`, `diag.18.Defer@L69778`, `grammar.18.Region@L69795`
- `rule.18.Parse-Region-Opts-None@L69814`, `rule.18.Parse-Region-Opts-Some@L69830`, `rule.18.Parse-Region-Alias-None@L69846`, `rule.18.Parse-Region-Alias-Some@L69862`, `rule.18.Parse-Region-Stmt@L69878`, `def.18.RegionStmtAst@L69896`, `def.18.RegionTypeAndFreshNameHelpers@L69909`, `def.18.RegionOptsExpr@L69923`
- `def.18.RegionBind@L69939`, `rule.18.T-RegionStmt@L69954`, `req.18.AnonymousRegionSyntheticBinding@L69970`, `req.18.RegionBindingState@L69983`, `req.18.RegionProvenance@L69996`, `def.18.BindRegionAlias@L70011`, `rule.18.ExecSigma-Region@L70025`, `rule.18.ExecSigma-Region-Ctrl@L70041`
- `def.18.RegionRelease@L70057`, `rule.18.Step-Exec-Region-Enter@L70070`, `rule.18.Step-Exec-Region-Enter-Ctrl@L70086`, `rule.18.Step-Exec-Region-Body@L70102`, `rule.18.Step-Exec-Region-Exit-Ok@L70118`, `rule.18.Step-Exec-Region-Exit-Ctrl@L70134`, `rule.18.Lower-Stmt-Region@L70152`, `diag.18.Region@L70170`
- `grammar.18.Frame@L70187`, `rule.18.Parse-Frame-Stmt@L70204`, `rule.18.Parse-Frame-Explicit@L70220`, `def.18.FrameStmtAst@L70238`, `def.18.InnermostActiveRegion@L70251`, `def.18.FrameBind@L70267`, `rule.18.T-FrameStmt-Implicit@L70284`, `rule.18.T-FrameStmt-Explicit@L70300`
- `rule.18.Frame-NoActiveRegion-Err@L70316`, `rule.18.Frame-Target-NotActive-Err@L70332`, `req.18.FrameSyntheticRegionBinding@L70348`, `req.18.FrameBindingState@L70361`, `req.18.FrameProvenance@L70374`, `def.18.FrameTargetResolution@L70389`, `def.18.FrameEnter@L70403`, `rule.18.ExecSigma-Frame-Implicit@L70416`
- `rule.18.ExecSigma-Frame-Explicit@L70432`, `def.18.FrameReset@L70448`, `rule.18.Step-Exec-Frame-Enter-Implicit@L70461`, `rule.18.Step-Exec-Frame-Enter-Explicit@L70477`, `rule.18.Step-Exec-Frame-Body@L70493`, `rule.18.Step-Exec-Frame-Exit-Ok@L70509`, `rule.18.Step-Exec-Frame-Exit-Ctrl@L70525`, `rule.18.Lower-Stmt-Frame-Implicit@L70543`
- `rule.18.Lower-Stmt-Frame-Explicit@L70559`, `diag.18.Frame@L70577`, `grammar.18.ControlTransferStatements@L70594`, `rule.18.Parse-Return-Stmt@L70613`, `rule.18.Parse-Break-Stmt@L70629`, `rule.18.Parse-Continue-Stmt@L70645`, `def.18.ControlTransferAstForms@L70663`, `rule.18.T-Return-Value@L70680`
- `rule.18.T-Return-Unit@L70696`, `rule.18.Return-Async-Type-Err@L70712`, `rule.18.Return-Async-Unit-Err@L70728`, `rule.18.Return-Type-Err@L70744`, `rule.18.Return-Unit-Err@L70760`, `rule.18.T-Break-Value@L70776`, `rule.18.T-Break-Unit@L70792`, `rule.18.Break-Outside-Loop@L70808`
- `rule.18.T-Continue@L70824`, `rule.18.Continue-Outside-Loop@L70840`, `req.18.ControlTransferBindingState@L70856`, `req.18.ControlTransferProvenance@L70869`, `rule.18.ExecSigma-Return@L70884`, `rule.18.ExecSigma-Return-Unit@L70900`, `rule.18.ExecSigma-Return-Ctrl@L70915`, `rule.18.ExecSigma-Break@L70931`
- `rule.18.ExecSigma-Break-Unit@L70947`, `rule.18.ExecSigma-Break-Ctrl@L70962`, `rule.18.ExecSigma-Continue@L70978`, `rule.18.Lower-Stmt-Return@L70995`, `rule.18.Lower-Stmt-Return-Unit@L71011`, `rule.18.Lower-Stmt-Break@L71026`, `rule.18.Lower-Stmt-Break-Unit@L71042`, `rule.18.Lower-Stmt-Continue@L71057`
- `req.18.ControlTransferTemporaryCleanupLowering@L71072`, `diag.18.ControlTransferStatements@L71092`, `grammar.18.UnsafeStatements@L71109`, `rule.18.Parse-Unsafe-Block@L71126`, `def.18.UnsafeBlockStmtAst@L71144`, `rule.18.T-UnsafeStmt@L71159`, `req.18.UnsafeStatementStateAndProvenance@L71175`, `diag.18.UnsafeRequiredOperationOwnership@L71188`
- `rule.18.ExecSigma-UnsafeStmt@L71203`, `rule.18.Lower-Stmt-UnsafeBlock@L71221`, `diag.18.UnsafeStatements@L71239`, `diag.18.StatementDiagnosticsSupplement@L71254`

#### `spec.key-system`

Count: 185 total; 175 required; 0 recommended; 0 informative. Ledger line span: L70903-L74064.

- `grammar.19.KeyPaths@L71288`, `parse.19.KeyPathRules@L71310`, `ast.19.KeyPathForms@L71336`, `requirement.19.KeyPathWellFormedness@L71361`, `requirement.19.KeyAnalysisSharedOnly@L71374`, `def.19.RootExtraction@L71389`, `def.19.ObjectIdentity@L71413`, `def.19.KeyPathFormation@L71434`
- `requirement.19.PointerDereferenceKeyAccess@L71450`, `requirement.19.SharedDynamicClassObjects@L71468`, `def.19.DynMethods@L71481`, `rule.19.K-Witness-Shared-WF@L71494`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71510`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71525`, `def.19.SharedDynamicMethodCallKeyPath@L71538`, `def.19.KeyLoweringForms@L71555`
- `rule.19.Lower-KeyPath@L71570`, `rule.19.Lower-KeyAccess-Uncovered@L71586`, `rule.19.Lower-KeyAccess-Covered@L71602`, `diagnostics.19.KeyPaths@L71620`, `grammar.19.KeyAcquisitionBlocks@L71644`, `requirement.19.OrderedKeyBlockModifier@L71664`, `parse.19.KeyBlockRules@L71679`, `ast.19.KeyBlockForms@L71712`
- `def.19.KeyTriple@L71745`, `rule.19.K-Mode-Read@L71771`, `rule.19.K-Mode-Write@L71787`, `requirement.19.RestrictiveContextApplies@L71803`, `def.19.ReadContexts@L71816`, `def.19.WriteContexts@L71838`, `def.19.KeyStateContext@L71861`, `def.19.Covered@L71884`
- `requirement.19.ValidKeyContext@L71899`, `rule.19.K-Acquire-New@L71918`, `rule.19.K-Acquire-Covered@L71934`, `requirement.19.KeyAcquisitionEvaluationOrder@L71950`, `rule.19.K-Block-Acquire@L71965`, `rule.19.K-Read-Block-No-Write@L71981`, `requirement.19.KeyCoarseningInlineMarker@L71999`, `rule.19.K-Coarsen-Inline@L72012`
- `requirement.19.FieldKeyBoundary@L72028`, `requirement.19.ClosureDependencySetConsumption@L72043`, `def.19.SharedCaptures@L72056`, `def.19.LocalClosureKeyPath@L72069`, `rule.19.K-Closure-Escape-Keys@L72086`, `requirement.19.EscapingClosureSharedLifetime@L72102`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72115`, `requirement.19.KeyBlockCanonicalOrderReferences@L72134`
- `def.19.KeyBlockRuntimeJudgments@L72147`, `def.19.AcquireKeysSigma@L72160`, `def.19.ReleaseKeysSigma@L72177`, `def.19.ModeOf@L72193`, `rule.19.ExecSigma-KeyBlock@L72210`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72226`, `rule.19.Step-Exec-KeyBlock-Enter@L72242`, `rule.19.Step-Exec-KeyBlock-Body@L72258`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72274`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72290`, `requirement.19.ScopeExitKeyRelease@L72306`, `requirement.19.LocalClosureInvocationSharedCaptures@L72321`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72339`, `def.19.LowerKeyPathsEmpty@L72359`, `def.19.LowerKeyPathsCons@L72372`, `rule.19.Lower-Stmt-KeyBlock@L72385`
- `requirement.19.KeyScopeBound@L72406`, `requirement.19.KeyEscapeRestrictions@L72421`, `requirement.19.FineGrainedKeyLoopWarning@L72436`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72449`, `diagnostics.19.KeyAcquisitionBlocks@L72462`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72492`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72507`, `def.19.PrefixAndDisjoint@L72522`
- `def.19.KeyPathOrdering@L72537`, `def.19.KeyCompatibility@L72572`, `def.19.IndexEquivalence@L72603`, `requirement.19.IndexEquivalenceConservativeSubset@L72626`, `rule.19.K-Disjoint-Safe@L72639`, `rule.19.K-Prefix-Coverage@L72655`, `def.19.DynamicIndexDisjointness@L72673`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72696`
- `rule.19.K-Dynamic-Index-Conflict@L72709`, `def.19.ReadThenWrite@L72727`, `requirement.19.ReadThenWriteDiagnosticSurface@L72744`, `requirement.19.ReadThenWriteOtherWriteForms@L72757`, `rule.19.K-Read-Write-Reject@L72770`, `rule.19.K-RMW-Permitted@L72786`, `rule.19.K-RMW-Explicit-Warn@L72802`, `rule.19.K-RMW-Contention-Warn@L72818`
- `def.19.OrderedComparablePaths@L72834`, `rule.19.K-Ordered-Ok@L72850`, `rule.19.K-Ordered-Base-Err@L72866`, `rule.19.K-Ordered-Redundant-Warn@L72882`, `requirement.19.CanonicalOrderDynamicUse@L72900`, `requirement.19.KeyConflictRuntimeCompatibility@L72913`, `def.19.LowerConflictChecks@L72928`, `rule.19.Lower-Key-ConflictChecks@L72945`
- `diagnostics.19.ConflictDetection@L72963`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72988`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L73003`, `ast.19.NestedReleaseForm@L73018`, `rule.19.K-Nested-Same-Path@L73033`, `def.19.SharedParam@L73054`, `def.19.DirectCalleeAccesses@L73068`, `def.19.CalleeAccessSummary@L73081`
- `def.19.CalleeAccessInstantiation@L73094`, `rule.19.K-Reentrant@L73109`, `requirement.19.UnknownCalleeAccessWarning@L73124`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73139`, `requirement.19.StaleOkSuppressesReleaseWarning@L73154`, `rule.19.K-Release-SameMode-Err@L73167`, `requirement.19.NestedReleaseExecutionSequence@L73185`, `rule.19.K-Release-Sequence@L73204`
- `requirement.19.NestedReleaseInterleavingWindow@L73224`, `def.19.HeldKeyAccessors@L73237`, `def.19.ReleasedKeyState@L73253`, `rule.19.ExecSigma-KeyBlock-Release@L73273`, `rule.19.Lower-Stmt-KeyBlock-Release@L73291`, `diagnostics.19.NestedRelease@L73314`, `grammar.19.SpeculativeExecution@L73337`, `parse.19.SpeculativeBlocks@L73354`
- `ast.19.SpeculativeBlockForm@L73369`, `def.19.SpeculativeSetsAndStates@L73382`, `rule.19.K-Spec-Write-Required@L73410`, `rule.19.K-Spec-Pure-Body@L73426`, `requirement.19.SpeculativePermittedOperations@L73442`, `requirement.19.SpeculativeProhibitedOperations@L73460`, `def.19.IsCallLike@L73480`, `rule.19.K-Spec-No-Nested-Key@L73493`
- `rule.19.K-Spec-No-Impure-Call@L73509`, `rule.19.K-Spec-No-Memory-Ordering@L73525`, `rule.19.K-Spec-No-Wait@L73541`, `rule.19.K-Spec-No-Defer@L73557`, `rule.19.K-Spec-No-Release@L73573`, `rule.19.ExecSigma-KeyBlock-Speculative@L73593`, `def.19.SpecLoop@L73609`, `rule.19.Spec-Start@L73630`
- `rule.19.Spec-Snapshot@L73645`, `rule.19.Spec-Exec-Ok@L73661`, `rule.19.Spec-Exec-Panic@L73677`, `rule.19.Spec-Commit-Success@L73693`, `rule.19.Spec-Commit-Fail-Retry@L73709`, `rule.19.Spec-Commit-Fail-Fallback@L73725`, `rule.19.Spec-Retry@L73741`, `rule.19.Spec-Fallback@L73756`
- `rule.19.SpecBlock-Ok@L73772`, `rule.19.SpecBlock-Panic@L73788`, `def.19.SpeculativeRuntimeHelpers@L73804`, `requirement.19.SpeculativePanicDiscardsWrites@L73825`, `requirement.19.SpeculativeAtomicity@L73838`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73851`, `def.19.SpeculativeIR@L73868`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73881`
- `diagnostics.19.SpeculativeExecution@L73901`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73929`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73944`, `def.19.StaticallySafeConditions@L73959`, `requirement.19.StaticallySafeSoundProofRequired@L73981`, `rule.19.K-Static-Safe@L74000`, `requirement.19.NoRuntimeSyncMeaning@L74016`, `rule.19.K-Static-Required@L74031`
- `requirement.19.RuntimeSynchronizationRequirements@L74049`, `requirement.19.DynamicIndexRuntimeOrdering@L74067`, `requirement.19.DynamicIndexedPathCoarsening@L74086`, `requirement.19.CanonicalOrderDeadlockFreedom@L74101`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74116`, `rule.19.K-Dynamic-Permitted@L74131`, `requirement.19.DynamicContextStaticSafeLowering@L74147`, `diagnostics.19.DynamicKeyVerification@L74162`
- `grammar.19.MemoryOrdering@L74183`, `parse.19.MemoryOrdering@L74203`, `ast.19.MemoryOrderingForms@L74220`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74243`, `def.19.MemoryOrderingLevels@L74258`, `requirement.19.MemoryOrderAttributeAttachment@L74279`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74297`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74310`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74323`, `rule.19.T-Fence@L74336`, `requirement.19.FenceContextAndHeldKeys@L74352`, `requirement.19.FenceEvaluation@L74367`, `requirement.19.FenceOrderingConstraints@L74384`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74401`, `rule.19.Lower-Expr-Fence@L74416`, `rule.19.Lower-Ordered-Access@L74431`
- `diagnostics.19.MemoryOrdering@L74449`
- `grammar.19.KeyPaths@L71288`, `parse.19.KeyPathRules@L71310`, `ast.19.KeyPathForms@L71336`, `requirement.19.KeyPathWellFormedness@L71361`, `requirement.19.KeyAnalysisSharedOnly@L71374`, `def.19.RootExtraction@L71389`, `def.19.ObjectIdentity@L71413`, `def.19.KeyPathFormation@L71434`
- `requirement.19.PointerDereferenceKeyAccess@L71450`, `requirement.19.SharedDynamicClassObjects@L71468`, `def.19.DynMethods@L71481`, `rule.19.K-Witness-Shared-WF@L71494`, `requirement.19.SharedDynamicClassRejectsMutatingReceivers@L71510`, `requirement.19.RuntimeKeyRootIdentityConstraints@L71525`, `def.19.SharedDynamicMethodCallKeyPath@L71538`, `def.19.KeyLoweringForms@L71555`
- `rule.19.Lower-KeyPath@L71570`, `rule.19.Lower-KeyAccess-Uncovered@L71586`, `rule.19.Lower-KeyAccess-Covered@L71602`, `diagnostics.19.KeyPaths@L71620`, `grammar.19.KeyAcquisitionBlocks@L71644`, `requirement.19.OrderedKeyBlockModifier@L71664`, `parse.19.KeyBlockRules@L71679`, `ast.19.KeyBlockForms@L71712`
- `def.19.KeyTriple@L71745`, `rule.19.K-Mode-Read@L71771`, `rule.19.K-Mode-Write@L71787`, `requirement.19.RestrictiveContextApplies@L71803`, `def.19.ReadContexts@L71816`, `def.19.WriteContexts@L71838`, `def.19.KeyStateContext@L71861`, `def.19.Covered@L71884`
- `requirement.19.ValidKeyContext@L71899`, `rule.19.K-Acquire-New@L71918`, `rule.19.K-Acquire-Covered@L71934`, `requirement.19.KeyAcquisitionEvaluationOrder@L71950`, `rule.19.K-Block-Acquire@L71965`, `rule.19.K-Read-Block-No-Write@L71981`, `requirement.19.KeyCoarseningInlineMarker@L71999`, `rule.19.K-Coarsen-Inline@L72012`
- `requirement.19.FieldKeyBoundary@L72028`, `requirement.19.ClosureDependencySetConsumption@L72043`, `def.19.SharedCaptures@L72056`, `def.19.LocalClosureKeyPath@L72069`, `rule.19.K-Closure-Escape-Keys@L72086`, `requirement.19.EscapingClosureSharedLifetime@L72102`, `requirement.19.EscapingClosureRuntimeIdentityCoverage@L72115`, `requirement.19.KeyBlockCanonicalOrderReferences@L72134`
- `def.19.KeyBlockRuntimeJudgments@L72147`, `def.19.AcquireKeysSigma@L72160`, `def.19.ReleaseKeysSigma@L72177`, `def.19.ModeOf@L72193`, `rule.19.ExecSigma-KeyBlock@L72210`, `rule.19.ExecSigma-KeyBlock-Ctrl@L72226`, `rule.19.Step-Exec-KeyBlock-Enter@L72242`, `rule.19.Step-Exec-KeyBlock-Body@L72258`
- `rule.19.Step-Exec-KeyBlock-Exit-Ok@L72274`, `rule.19.Step-Exec-KeyBlock-Exit-Ctrl@L72290`, `requirement.19.ScopeExitKeyRelease@L72306`, `requirement.19.LocalClosureInvocationSharedCaptures@L72321`, `requirement.19.EscapingClosureInvocationSharedCaptures@L72339`, `def.19.LowerKeyPathsEmpty@L72359`, `def.19.LowerKeyPathsCons@L72372`, `rule.19.Lower-Stmt-KeyBlock@L72385`
- `requirement.19.KeyScopeBound@L72406`, `requirement.19.KeyEscapeRestrictions@L72421`, `requirement.19.FineGrainedKeyLoopWarning@L72436`, `requirement.19.KeyEscapeDiagnosticPrecedence@L72449`, `diagnostics.19.KeyAcquisitionBlocks@L72462`, `requirement.19.ConflictDetectionNoAdditionalSyntax@L72492`, `requirement.19.ConflictDetectionNoAdditionalParsingRules@L72507`, `def.19.PrefixAndDisjoint@L72522`
- `def.19.KeyPathOrdering@L72537`, `def.19.KeyCompatibility@L72572`, `def.19.IndexEquivalence@L72603`, `requirement.19.IndexEquivalenceConservativeSubset@L72626`, `rule.19.K-Disjoint-Safe@L72639`, `rule.19.K-Prefix-Coverage@L72655`, `def.19.DynamicIndexDisjointness@L72673`, `requirement.19.DynamicIndexDisjointnessConservativeSubset@L72696`
- `rule.19.K-Dynamic-Index-Conflict@L72709`, `def.19.ReadThenWrite@L72727`, `requirement.19.ReadThenWriteDiagnosticSurface@L72744`, `requirement.19.ReadThenWriteOtherWriteForms@L72757`, `rule.19.K-Read-Write-Reject@L72770`, `rule.19.K-RMW-Permitted@L72786`, `rule.19.K-RMW-Explicit-Warn@L72802`, `rule.19.K-RMW-Contention-Warn@L72818`
- `def.19.OrderedComparablePaths@L72834`, `rule.19.K-Ordered-Ok@L72850`, `rule.19.K-Ordered-Base-Err@L72866`, `rule.19.K-Ordered-Redundant-Warn@L72882`, `requirement.19.CanonicalOrderDynamicUse@L72900`, `requirement.19.KeyConflictRuntimeCompatibility@L72913`, `def.19.LowerConflictChecks@L72928`, `rule.19.Lower-Key-ConflictChecks@L72945`
- `diagnostics.19.ConflictDetection@L72963`, `requirement.19.NestedReleaseNoAdditionalSyntax@L72988`, `requirement.19.NestedReleaseNoAdditionalParsingRules@L73003`, `ast.19.NestedReleaseForm@L73018`, `rule.19.K-Nested-Same-Path@L73033`, `def.19.SharedParam@L73054`, `def.19.DirectCalleeAccesses@L73068`, `def.19.CalleeAccessSummary@L73081`
- `def.19.CalleeAccessInstantiation@L73094`, `rule.19.K-Reentrant@L73109`, `requirement.19.UnknownCalleeAccessWarning@L73124`, `rule.19.CallSharedArgumentNoKeyAcquisition@L73139`, `requirement.19.StaleOkSuppressesReleaseWarning@L73154`, `rule.19.K-Release-SameMode-Err@L73167`, `requirement.19.NestedReleaseExecutionSequence@L73185`, `rule.19.K-Release-Sequence@L73204`
- `requirement.19.NestedReleaseInterleavingWindow@L73224`, `def.19.HeldKeyAccessors@L73237`, `def.19.ReleasedKeyState@L73253`, `rule.19.ExecSigma-KeyBlock-Release@L73273`, `rule.19.Lower-Stmt-KeyBlock-Release@L73291`, `diagnostics.19.NestedRelease@L73314`, `grammar.19.SpeculativeExecution@L73337`, `parse.19.SpeculativeBlocks@L73354`
- `ast.19.SpeculativeBlockForm@L73369`, `def.19.SpeculativeSetsAndStates@L73382`, `rule.19.K-Spec-Write-Required@L73410`, `rule.19.K-Spec-Pure-Body@L73426`, `requirement.19.SpeculativePermittedOperations@L73442`, `requirement.19.SpeculativeProhibitedOperations@L73460`, `def.19.IsCallLike@L73480`, `rule.19.K-Spec-No-Nested-Key@L73493`
- `rule.19.K-Spec-No-Impure-Call@L73509`, `rule.19.K-Spec-No-Memory-Ordering@L73525`, `rule.19.K-Spec-No-Wait@L73541`, `rule.19.K-Spec-No-Defer@L73557`, `rule.19.K-Spec-No-Release@L73573`, `rule.19.ExecSigma-KeyBlock-Speculative@L73593`, `def.19.SpecLoop@L73609`, `rule.19.Spec-Start@L73630`
- `rule.19.Spec-Snapshot@L73645`, `rule.19.Spec-Exec-Ok@L73661`, `rule.19.Spec-Exec-Panic@L73677`, `rule.19.Spec-Commit-Success@L73693`, `rule.19.Spec-Commit-Fail-Retry@L73709`, `rule.19.Spec-Commit-Fail-Fallback@L73725`, `rule.19.Spec-Retry@L73741`, `rule.19.Spec-Fallback@L73756`
- `rule.19.SpecBlock-Ok@L73772`, `rule.19.SpecBlock-Panic@L73788`, `def.19.SpeculativeRuntimeHelpers@L73804`, `requirement.19.SpeculativePanicDiscardsWrites@L73825`, `requirement.19.SpeculativeAtomicity@L73838`, `requirement.19.SpeculativeAbstractSemanticsAndFallback@L73851`, `def.19.SpeculativeIR@L73868`, `rule.19.Lower-Stmt-KeyBlock-Speculative@L73881`
- `diagnostics.19.SpeculativeExecution@L73901`, `requirement.19.DynamicKeyVerificationNoAdditionalSyntax@L73929`, `requirement.19.DynamicKeyVerificationNoAdditionalParsingRules@L73944`, `def.19.StaticallySafeConditions@L73959`, `requirement.19.StaticallySafeSoundProofRequired@L73981`, `rule.19.K-Static-Safe@L74000`, `requirement.19.NoRuntimeSyncMeaning@L74016`, `rule.19.K-Static-Required@L74031`
- `requirement.19.RuntimeSynchronizationRequirements@L74049`, `requirement.19.DynamicIndexRuntimeOrdering@L74067`, `requirement.19.DynamicIndexedPathCoarsening@L74086`, `requirement.19.CanonicalOrderDeadlockFreedom@L74101`, `requirement.19.StaticAndRuntimeKeySafetyEquivalence@L74116`, `rule.19.K-Dynamic-Permitted@L74131`, `requirement.19.DynamicContextStaticSafeLowering@L74147`, `diagnostics.19.DynamicKeyVerification@L74162`
- `grammar.19.MemoryOrdering@L74183`, `parse.19.MemoryOrdering@L74203`, `ast.19.MemoryOrderingForms@L74220`, `requirement.19.MemoryOrderingDefaultsAndKeySemantics@L74243`, `def.19.MemoryOrderingLevels@L74258`, `requirement.19.MemoryOrderAttributeAttachment@L74279`, `requirement.19.ExpressionMemoryOrderWellFormedness@L74297`, `requirement.19.MemoryOrderDoesNotAlterKeySemantics@L74310`
- `requirement.19.MemoryOrderNotInsideSpeculativeBlocks@L74323`, `rule.19.T-Fence@L74336`, `requirement.19.FenceContextAndHeldKeys@L74352`, `requirement.19.FenceEvaluation@L74367`, `requirement.19.FenceOrderingConstraints@L74384`, `requirement.19.FenceNoProgramVisibleStorageAccess@L74401`, `rule.19.Lower-Expr-Fence@L74416`, `rule.19.Lower-Ordered-Access@L74431`
- `diagnostics.19.MemoryOrdering@L74449`

#### `spec.structured-parallelism`

Count: 181 total; 180 required; 0 recommended; 0 informative. Ledger line span: L74083-L77316.

- `grammar.20.ParallelBlocks@L74468`, `parse.20.ParallelBlockRules@L74493`, `ast.20.ParallelBlockForms@L74523`, `def.20.ParallelBlockOptionValidation@L74559`, `rule.20.Dim3Const-Err@L74585`, `def.20.ParallelDomainCtorValidation@L74601`, `rule.20.T-Parallel@L74621`, `requirement.20.ParallelBlockWellFormedness@L74637`
- `rule.20.Parallel-Domain-Param-Err@L74654`, `requirement.20.ParallelCancelOptionType@L74670`, `def.20.ParallelState@L74685`, `def.20.ParallelGpuTopologyOptions@L74704`, `def.20.AwaitSpawned@L74736`, `rule.20.EvalSigma-Parallel@L74749`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74765`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74781`
- `requirement.20.ParallelPanicPropagationReference@L74797`, `def.20.ParallelLoweringJudgments@L74812`, `rule.20.Lower-Expr-Parallel@L74825`, `diagnostics.20.ParallelBlocks@L74843`, `requirement.20.ExecutionDomainSyntax@L74864`, `grammar.20.ExecutionDomainExamples@L74877`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74896`, `parse.20.GpuPtrGenericType@L74909`
- `def.20.GpuDomainJudgments@L74924`, `def.20.GpuMemoryForms@L74943`, `def.20.GpuPtrType@L74963`, `def.20.DispatchGpuTopologyComputation@L74977`, `def.20.GpuExecutionTopology@L75000`, `def.20.GpuIntrinsicTable@L75023`, `def.20.GpuRuntimeState@L75049`, `def.20.ExecutionDomainClass@L75075`
- `requirement.20.ExecutionDomainContextMethods@L75095`, `def.20.GpuSafeType@L75122`, `def.20.GpuSafePredicateClauses@L75151`, `rule.20.GpuSafe-Prim@L75166`, `rule.20.GpuSafe-RawPtr@L75182`, `rule.20.GpuSafe-Array@L75198`, `rule.20.GpuSafe-Tuple@L75214`, `rule.20.GpuSafe-Perm@L75230`
- `rule.20.GpuSafe-Record@L75246`, `rule.20.GpuSafe-Enum@L75262`, `rule.20.GpuSafe-StringView@L75278`, `rule.20.GpuSafe-BytesView@L75294`, `rule.20.GpuSafeType-Err@L75310`, `rule.20.GpuSafe-Record-Field-Err@L75326`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75342`, `rule.20.T-GpuIntrinsic@L75358`
- `rule.20.Barrier-Outside-Err@L75374`, `rule.20.GpuIntrinsic-Outside-Err@L75390`, `rule.20.GpuPtr-AddrSpace-Err@L75406`, `requirement.20.ExecutionDomainDispatchableClass@L75422`, `requirement.20.GpuSafeGenericBounds@L75435`, `requirement.20.KeySystemUnavailableInGpuContext@L75448`, `requirement.20.InlineDomainSemantics@L75463`, `def.20.GpuMemoryVisibility@L75481`
- `rule.20.GpuPtr-Deref-Visible@L75497`, `rule.20.GpuPtr-Deref-Err@L75513`, `def.20.GpuTopologyValidity@L75529`, `rule.20.EvalSigma-GPU-Parallel@L75548`, `rule.20.EvalSigma-GPU-Dispatch@L75564`, `rule.20.GpuExecute-Step@L75580`, `rule.20.GpuBarrier-Sync@L75596`, `requirement.20.GpuBarrierWait@L75612`
- `rule.20.EvalSigma-GpuBarrier@L75627`, `rule.20.Barrier-Divergence-Err@L75643`, `rule.20.KeyBlock-GPU-Err@L75659`, `rule.20.WorkgroupSize-Err@L75675`, `rule.20.Lower-Domain-CPU@L75693`, `rule.20.Lower-Domain-GPU@L75708`, `rule.20.Lower-Domain-Inline@L75723`, `rule.20.Lower-Expr-Parallel-GPU@L75738`
- `rule.20.Lower-Expr-GpuBarrier@L75754`, `diagnostics.20.ExecutionDomains@L75771`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75799`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75814`, `requirement.20.CaptureSetComputationReference@L75829`, `def.20.GpuCaptureJudgments@L75851`, `requirement.20.ParallelCapturePermissions@L75868`, `rule.20.Parallel-Closure-Capture-Const@L75885`
- `rule.20.Parallel-Closure-Capture-Shared@L75901`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75917`, `def.20.OuterParallelMoveSelection@L75933`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75947`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75963`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75979`, `requirement.20.ParallelClosuresLocalForKeys@L75995`, `rule.20.GpuCaptureOk-Const@L76008`
- `rule.20.GpuCaptureOk-Unique-Move@L76024`, `rule.20.GpuCapture-Shared-Err@L76040`, `rule.20.GpuCapture-HeapProv-Err@L76056`, `rule.20.GpuCapture-NonGpuSafe-Err@L76072`, `requirement.20.MovedBindingValidityReference@L76088`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76103`, `requirement.20.CaptureSemanticsGenericLowering@L76122`, `diagnostics.20.CaptureSemantics@L76137`
- `grammar.20.Spawn@L76161`, `parse.20.SpawnRules@L76182`, `ast.20.SpawnForms@L76209`, `def.20.SpawnOptionValidation@L76242`, `requirement.20.SpawnRequiresParallelContext@L76261`, `rule.20.T-Spawn@L76274`, `def.20.SpawnHandleAndEnqueue@L76292`, `requirement.20.SpawnEvaluationProcedure@L76309`
- `rule.20.EvalSigma-Spawn@L76329`, `requirement.20.SpawnedResultRetrievalReference@L76345`, `rule.20.Lower-Expr-Spawn@L76360`, `diagnostics.20.Spawn@L76378`, `grammar.20.Dispatch@L76397`, `parse.20.DispatchRules@L76422`, `ast.20.DispatchForms@L76456`, `requirement.20.DispatchRequiresParallelContext@L76499`
- `rule.20.T-Dispatch@L76512`, `rule.20.T-Dispatch-Reduce@L76528`, `rule.20.T-GPU-Dispatch@L76544`, `rule.20.T-GPU-Dispatch-Reduce@L76560`, `def.20.DispatchAccessInference@L76576`, `def.20.DispatchOptionsAndDynamicKeys@L76614`, `rule.20.Dispatch-Infer-Err@L76645`, `rule.20.Dispatch-Outside-Err@L76661`
- `rule.20.Dispatch-Chunk-Type-Err@L76677`, `rule.20.Dispatch-Dependency-Err@L76693`, `rule.20.Dispatch-Reduce-Assoc-Err@L76709`, `rule.20.Dispatch-DynamicKey-Warn@L76725`, `requirement.20.DispatchKeyInferenceRequired@L76741`, `rule.20.DispatchIndexedDisjointness@L76754`, `requirement.20.DispatchReductionAssociativity@L76769`, `requirement.20.DispatchChunkSemanticsStatic@L76782`
- `def.20.DispatchPartitionSpec@L76797`, `def.20.DispatchIndexAndPathDisjointness@L76812`, `def.20.DispatchPartitioning@L76855`, `def.20.DispatchReductionAndChunking@L76876`, `rule.20.EvalSigma-Dispatch@L76898`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76914`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76930`, `def.20.DispatchRun@L76946`
- `rule.20.Lower-Expr-Dispatch@L76967`, `diagnostics.20.Dispatch@L76985`, `requirement.20.CancellationSyntax@L77008`, `requirement.20.CancellationNoAdditionalParsingRules@L77023`, `ast.20.CancelTokenForms@L77038`, `requirement.20.CancelTokenStaticSemantics@L77065`, `requirement.20.CancelTokenParallelAvailability@L77090`, `def.20.CancelRuntimeHelpers@L77103`
- `rule.20.Cancel-New@L77126`, `rule.20.Cancel-Child@L77142`, `rule.20.Cancel-IsCancelled@L77158`, `rule.20.Cancel-DoCancel@L77174`, `rule.20.Cancel-WaitCancelled-Completed@L77190`, `rule.20.Cancel-WaitCancelled-Suspended@L77206`, `requirement.20.CooperativeCancellationBehavior@L77222`, `def.20.CancelIR@L77244`
- `rule.20.Lower-Cancel-New@L77257`, `rule.20.Lower-Cancel-Request@L77272`, `rule.20.Lower-Cancel-Wait@L77287`, `requirement.20.CancellationCheckpointLowering@L77302`, `requirement.20.SpawnDispatchCancellationLowering@L77315`, `diagnostics.20.Cancellation@L77330`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77347`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77362`
- `ast.20.ParallelPanicPropagationInputs@L77377`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77392`, `requirement.20.ParallelWorkItemPanicSemantics@L77407`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77424`, `requirement.20.ParallelPanicCancellationRequest@L77440`, `def.20.FirstCompletedFailure@L77453`, `rule.20.Lower-Parallel-Join-Panic@L77468`, `diagnostics.20.PanicHandling@L77485`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77502`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77517`, `ast.20.DeterminismNestingForms@L77532`, `requirement.20.DispatchDeterminismConditions@L77547`, `requirement.20.OrderedDispatchSequentialSideEffects@L77564`, `requirement.20.NoNestedGpuParallel@L77577`, `requirement.20.NestedParallelRuntimeSemantics@L77592`, `def.20.ParallelDeterministicOrdering@L77613`
- `rule.20.Lower-Deterministic-Dispatch@L77639`, `rule.20.Lower-Nested-Parallel@L77655`, `diagnostics.20.DeterminismAndNesting@L77671`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77688`, `diagnostics.20.StructuredParallelismSupplement@L77701`
- `grammar.20.ParallelBlocks@L74468`, `parse.20.ParallelBlockRules@L74493`, `ast.20.ParallelBlockForms@L74523`, `def.20.ParallelBlockOptionValidation@L74559`, `rule.20.Dim3Const-Err@L74585`, `def.20.ParallelDomainCtorValidation@L74601`, `rule.20.T-Parallel@L74621`, `requirement.20.ParallelBlockWellFormedness@L74637`
- `rule.20.Parallel-Domain-Param-Err@L74654`, `requirement.20.ParallelCancelOptionType@L74670`, `def.20.ParallelState@L74685`, `def.20.ParallelGpuTopologyOptions@L74704`, `def.20.AwaitSpawned@L74736`, `rule.20.EvalSigma-Parallel@L74749`, `rule.20.EvalSigma-Parallel-Body-Ctrl@L74765`, `rule.20.EvalSigma-Parallel-Domain-Ctrl@L74781`
- `requirement.20.ParallelPanicPropagationReference@L74797`, `def.20.ParallelLoweringJudgments@L74812`, `rule.20.Lower-Expr-Parallel@L74825`, `diagnostics.20.ParallelBlocks@L74843`, `requirement.20.ExecutionDomainSyntax@L74864`, `grammar.20.ExecutionDomainExamples@L74877`, `requirement.20.ExecutionDomainsNoAdditionalParsingProductions@L74896`, `parse.20.GpuPtrGenericType@L74909`
- `def.20.GpuDomainJudgments@L74924`, `def.20.GpuMemoryForms@L74943`, `def.20.GpuPtrType@L74963`, `def.20.DispatchGpuTopologyComputation@L74977`, `def.20.GpuExecutionTopology@L75000`, `def.20.GpuIntrinsicTable@L75023`, `def.20.GpuRuntimeState@L75049`, `def.20.ExecutionDomainClass@L75075`
- `requirement.20.ExecutionDomainContextMethods@L75095`, `def.20.GpuSafeType@L75122`, `def.20.GpuSafePredicateClauses@L75151`, `rule.20.GpuSafe-Prim@L75166`, `rule.20.GpuSafe-RawPtr@L75182`, `rule.20.GpuSafe-Array@L75198`, `rule.20.GpuSafe-Tuple@L75214`, `rule.20.GpuSafe-Perm@L75230`
- `rule.20.GpuSafe-Record@L75246`, `rule.20.GpuSafe-Enum@L75262`, `rule.20.GpuSafe-StringView@L75278`, `rule.20.GpuSafe-BytesView@L75294`, `rule.20.GpuSafeType-Err@L75310`, `rule.20.GpuSafe-Record-Field-Err@L75326`, `rule.20.GpuSafe-Generic-Unbounded-Err@L75342`, `rule.20.T-GpuIntrinsic@L75358`
- `rule.20.Barrier-Outside-Err@L75374`, `rule.20.GpuIntrinsic-Outside-Err@L75390`, `rule.20.GpuPtr-AddrSpace-Err@L75406`, `requirement.20.ExecutionDomainDispatchableClass@L75422`, `requirement.20.GpuSafeGenericBounds@L75435`, `requirement.20.KeySystemUnavailableInGpuContext@L75448`, `requirement.20.InlineDomainSemantics@L75463`, `def.20.GpuMemoryVisibility@L75481`
- `rule.20.GpuPtr-Deref-Visible@L75497`, `rule.20.GpuPtr-Deref-Err@L75513`, `def.20.GpuTopologyValidity@L75529`, `rule.20.EvalSigma-GPU-Parallel@L75548`, `rule.20.EvalSigma-GPU-Dispatch@L75564`, `rule.20.GpuExecute-Step@L75580`, `rule.20.GpuBarrier-Sync@L75596`, `requirement.20.GpuBarrierWait@L75612`
- `rule.20.EvalSigma-GpuBarrier@L75627`, `rule.20.Barrier-Divergence-Err@L75643`, `rule.20.KeyBlock-GPU-Err@L75659`, `rule.20.WorkgroupSize-Err@L75675`, `rule.20.Lower-Domain-CPU@L75693`, `rule.20.Lower-Domain-GPU@L75708`, `rule.20.Lower-Domain-Inline@L75723`, `rule.20.Lower-Expr-Parallel-GPU@L75738`
- `rule.20.Lower-Expr-GpuBarrier@L75754`, `diagnostics.20.ExecutionDomains@L75771`, `requirement.20.CaptureSemanticsNoAdditionalSyntax@L75799`, `requirement.20.CaptureSemanticsNoAdditionalParsingRules@L75814`, `requirement.20.CaptureSetComputationReference@L75829`, `def.20.GpuCaptureJudgments@L75851`, `requirement.20.ParallelCapturePermissions@L75868`, `rule.20.Parallel-Closure-Capture-Const@L75885`
- `rule.20.Parallel-Closure-Capture-Shared@L75901`, `rule.20.Parallel-Closure-Capture-Unique-Err@L75917`, `def.20.OuterParallelMoveSelection@L75933`, `rule.20.Parallel-Closure-Capture-Unique-Move-Ok@L75947`, `rule.20.Parallel-Closure-Capture-OuterMove-Err@L75963`, `rule.20.Parallel-Escaping-Closure-Spawn-Err@L75979`, `requirement.20.ParallelClosuresLocalForKeys@L75995`, `rule.20.GpuCaptureOk-Const@L76008`
- `rule.20.GpuCaptureOk-Unique-Move@L76024`, `rule.20.GpuCapture-Shared-Err@L76040`, `rule.20.GpuCapture-HeapProv-Err@L76056`, `rule.20.GpuCapture-NonGpuSafe-Err@L76072`, `requirement.20.MovedBindingValidityReference@L76088`, `requirement.20.CaptureSemanticsNoAdditionalRuntimeMechanism@L76103`, `requirement.20.CaptureSemanticsGenericLowering@L76122`, `diagnostics.20.CaptureSemantics@L76137`
- `grammar.20.Spawn@L76161`, `parse.20.SpawnRules@L76182`, `ast.20.SpawnForms@L76209`, `def.20.SpawnOptionValidation@L76242`, `requirement.20.SpawnRequiresParallelContext@L76261`, `rule.20.T-Spawn@L76274`, `def.20.SpawnHandleAndEnqueue@L76292`, `requirement.20.SpawnEvaluationProcedure@L76309`
- `rule.20.EvalSigma-Spawn@L76329`, `requirement.20.SpawnedResultRetrievalReference@L76345`, `rule.20.Lower-Expr-Spawn@L76360`, `diagnostics.20.Spawn@L76378`, `grammar.20.Dispatch@L76397`, `parse.20.DispatchRules@L76422`, `ast.20.DispatchForms@L76456`, `requirement.20.DispatchRequiresParallelContext@L76499`
- `rule.20.T-Dispatch@L76512`, `rule.20.T-Dispatch-Reduce@L76528`, `rule.20.T-GPU-Dispatch@L76544`, `rule.20.T-GPU-Dispatch-Reduce@L76560`, `def.20.DispatchAccessInference@L76576`, `def.20.DispatchOptionsAndDynamicKeys@L76614`, `rule.20.Dispatch-Infer-Err@L76645`, `rule.20.Dispatch-Outside-Err@L76661`
- `rule.20.Dispatch-Chunk-Type-Err@L76677`, `rule.20.Dispatch-Dependency-Err@L76693`, `rule.20.Dispatch-Reduce-Assoc-Err@L76709`, `rule.20.Dispatch-DynamicKey-Warn@L76725`, `requirement.20.DispatchKeyInferenceRequired@L76741`, `rule.20.DispatchIndexedDisjointness@L76754`, `requirement.20.DispatchReductionAssociativity@L76769`, `requirement.20.DispatchChunkSemanticsStatic@L76782`
- `def.20.DispatchPartitionSpec@L76797`, `def.20.DispatchIndexAndPathDisjointness@L76812`, `def.20.DispatchPartitioning@L76855`, `def.20.DispatchReductionAndChunking@L76876`, `rule.20.EvalSigma-Dispatch@L76898`, `rule.20.EvalSigma-Dispatch-Range-Ctrl@L76914`, `rule.20.EvalSigma-Dispatch-Chunk-Ctrl@L76930`, `def.20.DispatchRun@L76946`
- `rule.20.Lower-Expr-Dispatch@L76967`, `diagnostics.20.Dispatch@L76985`, `requirement.20.CancellationSyntax@L77008`, `requirement.20.CancellationNoAdditionalParsingRules@L77023`, `ast.20.CancelTokenForms@L77038`, `requirement.20.CancelTokenStaticSemantics@L77065`, `requirement.20.CancelTokenParallelAvailability@L77090`, `def.20.CancelRuntimeHelpers@L77103`
- `rule.20.Cancel-New@L77126`, `rule.20.Cancel-Child@L77142`, `rule.20.Cancel-IsCancelled@L77158`, `rule.20.Cancel-DoCancel@L77174`, `rule.20.Cancel-WaitCancelled-Completed@L77190`, `rule.20.Cancel-WaitCancelled-Suspended@L77206`, `requirement.20.CooperativeCancellationBehavior@L77222`, `def.20.CancelIR@L77244`
- `rule.20.Lower-Cancel-New@L77257`, `rule.20.Lower-Cancel-Request@L77272`, `rule.20.Lower-Cancel-Wait@L77287`, `requirement.20.CancellationCheckpointLowering@L77302`, `requirement.20.SpawnDispatchCancellationLowering@L77315`, `diagnostics.20.Cancellation@L77330`, `requirement.20.PanicHandlingNoAdditionalSyntax@L77347`, `requirement.20.PanicHandlingNoAdditionalParsingRules@L77362`
- `ast.20.ParallelPanicPropagationInputs@L77377`, `requirement.20.PanicHandlingNoAdditionalStaticTypingRules@L77392`, `requirement.20.ParallelWorkItemPanicSemantics@L77407`, `rule.20.EvalSigma-Parallel-Spawn-Panic@L77424`, `requirement.20.ParallelPanicCancellationRequest@L77440`, `def.20.FirstCompletedFailure@L77453`, `rule.20.Lower-Parallel-Join-Panic@L77468`, `diagnostics.20.PanicHandling@L77485`
- `requirement.20.DeterminismNestingNoAdditionalSyntax@L77502`, `requirement.20.DeterminismNestingNoAdditionalParsingRules@L77517`, `ast.20.DeterminismNestingForms@L77532`, `requirement.20.DispatchDeterminismConditions@L77547`, `requirement.20.OrderedDispatchSequentialSideEffects@L77564`, `requirement.20.NoNestedGpuParallel@L77577`, `requirement.20.NestedParallelRuntimeSemantics@L77592`, `def.20.ParallelDeterministicOrdering@L77613`
- `rule.20.Lower-Deterministic-Dispatch@L77639`, `rule.20.Lower-Nested-Parallel@L77655`, `diagnostics.20.DeterminismAndNesting@L77671`, `requirement.20.StructuredParallelismRuntimePanicOwnership@L77688`, `diagnostics.20.StructuredParallelismSupplement@L77701`

#### `spec.async`

Count: 254 total; 253 required; 0 recommended; 0 informative. Ledger line span: L77337-L82007.

- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77722`, `requirement.21.ReservedAsyncTypeConstructors@L77735`, `requirement.21.AsyncParameterDefaults@L77755`, `requirement.21.ReservedAsyncStates@L77768`, `parse.21.AsyncTypes@L77787`, `parse.21.UnappliedAsyncPath@L77802`, `ast.21.AsyncModalDeclaration@L77817`, `ast.21.AsyncAliases@L77888`
- `ast.21.AsyncCombinatorMembers@L77909`, `def.21.AsyncSigAndBodyReturnType@L77929`, `rule.21.Sub-Async@L77956`, `rule.21.WF-Async@L77977`, `rule.21.WF-Async-ArgCount-Err@L77995`, `rule.21.WF-Async-Arg-WF-Err@L78013`, `rule.21.WF-Async-Path-Err@L78031`, `requirement.21.AsyncFailedUninhabitedForNeverError@L78049`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L78064`, `def.21.AsyncTypeLoweringForms@L78081`, `requirement.21.AsyncNeverErrorLowering@L78112`, `rule.21.Lower-Async-Type@L78125`, `rule.21.Lower-Async-Alias@L78145`, `diagnostics.21.AsyncType@L78165`, `grammar.21.SuspensionForms@L78184`, `parse.21.SuspensionFormsPrimaryExpressions@L78203`
- `rule.21.Parse-Wait-Expr@L78218`, `rule.21.Parse-Yield-From-Expr@L78238`, `rule.21.Parse-Yield-Expr@L78261`, `ast.21.SuspensionForms@L78284`, `ast.21.SuspensionFormResolution@L78304`, `ast.21.SuspensionFormEvaluationOrder@L78323`, `rule.21.T-Wait@L78346`, `rule.21.T-Wait-Future@L78364`
- `rule.21.Wait-Handle-Err@L78382`, `rule.21.T-Yield@L78402`, `rule.21.Yield-NotAsync-Err@L78420`, `rule.21.Yield-Out-Err@L78438`, `rule.21.T-Yield-From@L78458`, `rule.21.YieldFrom-NotAsync-Err@L78476`, `rule.21.YieldFrom-Out-Err@L78494`, `rule.21.YieldFrom-In-Err@L78513`
- `rule.21.YieldFrom-ErrType-Err@L78531`, `requirement.21.SuspensionKeyRestrictionsReference@L78549`, `requirement.21.WaitRuntimeSemantics@L78564`, `def.21.WaitRuntimeHelpers@L78586`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78610`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78628`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78647`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78660`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78678`, `rule.21.EvalSigma-Wait-Ctrl@L78697`, `requirement.21.YieldRuntimeSemantics@L78715`, `def.21.ResumptionHelpers@L78735`, `rule.21.EvalSigma-Yield@L78770`, `rule.21.EvalSigma-Yield-Release@L78789`, `rule.21.EvalSigma-Yield-Resume@L78808`, `requirement.21.YieldFromRuntimeSemantics@L78827`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78847`, `rule.21.EvalSigma-YieldFrom-Completed@L78867`, `rule.21.EvalSigma-YieldFrom-Failed@L78885`, `rule.21.EvalSigma-YieldFrom-Resume@L78903`, `def.21.EvalYieldFromContinueSignature@L78922`, `rule.21.EvalYieldFromContinue-Suspended@L78937`, `rule.21.EvalYieldFromContinue-Completed@L78957`, `rule.21.EvalYieldFromContinue-Failed@L78975`
- `def.21.SuspensionLoweringForms@L78995`, `rule.21.Lower-Wait-Spawned@L79016`, `rule.21.Lower-Wait-Tracked@L79034`, `rule.21.Lower-Yield@L79052`, `rule.21.Lower-Yield-Release@L79070`, `requirement.21.YieldReleaseReacquireLowering@L79088`, `rule.21.Lower-YieldFrom@L79101`, `requirement.21.YieldFromEnterLoweringLoop@L79119`
- `diagnostics.21.SuspensionForms@L79137`, `requirement.21.AsyncIterationSyntax@L79162`, `grammar.21.CompositionForms@L79177`, `requirement.21.AsyncMethodCallSurfaces@L79196`, `requirement.21.UntilMethodCallSurface@L79216`, `parse.21.CompositionPrimaryExpressions@L79231`, `rule.21.Parse-Sync-Expr@L79246`, `rule.21.Parse-Race-Expr@L79266`
- `rule.21.Parse-RaceArms-Cons@L79286`, `rule.21.Parse-RaceArm@L79304`, `rule.21.Parse-RaceArmsTail-End@L79324`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79342`, `rule.21.Parse-RaceArmsTail-Comma@L79360`, `rule.21.Parse-RaceHandler-Yield@L79378`, `rule.21.Parse-RaceHandler-Return@L79396`, `rule.21.Parse-All-Expr@L79416`
- `parse.21.CompositionOrdinarySurfaces@L79434`, `ast.21.CompositionForms@L79449`, `ast.21.AsyncIterationLoopForm@L79471`, `ast.21.CompositionMethodCallForms@L79488`, `ast.21.CompositionResolution@L79503`, `ast.21.CompositionEvaluationOrder@L79532`, `rule.21.T-Loop-Iter-Async@L79558`, `rule.21.Loop-Async-Err@L79580`
- `requirement.21.ManualSteppingRequirement@L79598`, `def.21.SyncYieldContainment@L79613`, `rule.21.Sync-Yield-Err@L79629`, `rule.21.Sync-YieldFrom-Err@L79647`, `rule.21.T-Sync@L79665`, `rule.21.Sync-Async-Context-Err@L79683`, `rule.21.Sync-Out-Err@L79701`, `rule.21.Sync-In-Err@L79720`
- `def.21.RaceMode@L79740`, `rule.21.T-Race@L79758`, `rule.21.T-Race-Stream@L79779`, `rule.21.Race-Arity-Err@L79800`, `rule.21.Race-Handler-Mix-Err@L79818`, `rule.21.Race-Operand-Out-Err@L79836`, `rule.21.Race-Operand-Err@L79855`, `rule.21.Race-Stream-Operand-Err@L79874`
- `rule.21.Race-Handler-Type-Err@L79893`, `rule.21.Race-Stream-Handler-Type-Err@L79914`, `rule.21.T-All@L79937`, `rule.21.All-Out-Err@L79956`, `rule.21.All-In-Err@L79974`, `def.21.UntilType@L79992`, `def.21.AsyncCombinatorTypes@L80009`, `requirement.21.AsyncCombinatorMemberLookup@L80030`
- `rule.21.T-Async-Map@L80045`, `rule.21.T-Async-Filter@L80063`, `rule.21.T-Async-Take@L80081`, `rule.21.T-Async-Fold@L80099`, `rule.21.T-Async-Chain@L80117`, `requirement.21.AsyncIterationRuntimeSemantics@L80137`, `requirement.21.ManualSteppingRuntimeSemantics@L80155`, `requirement.21.SyncRuntimeSemantics@L80168`
- `def.21.SyncStepSignature@L80188`, `rule.21.SyncStep-Suspended@L80203`, `rule.21.EvalSigma-Sync-Suspended@L80221`, `rule.21.EvalSigma-Sync-Completed@L80240`, `rule.21.EvalSigma-Sync-Failed@L80258`, `requirement.21.RaceReturnRuntimeSemantics@L80276`, `requirement.21.RaceStreamingRuntimeSemantics@L80294`, `def.21.RaceSelectionAndState@L80315`
- `rule.21.InitRace@L80345`, `def.21.RaceStepReturnSignature@L80364`, `rule.21.RaceStepReturn-Completed@L80379`, `rule.21.RaceStepReturn-Failed@L80399`, `rule.21.RaceStepReturn-Continue@L80418`, `rule.21.EvalSigma-Race-Return@L80438`, `def.21.RaceStepStreamSignature@L80457`, `rule.21.RaceStepStream-Yield-Initial@L80472`
- `rule.21.RaceStepStream-AllComplete@L80492`, `rule.21.RaceStepStream-Failed@L80510`, `rule.21.EvalSigma-Race-Stream@L80529`, `def.21.CancelAllSignature@L80548`, `rule.21.CancelAll@L80563`, `def.21.RaceStreamSuspensionState@L80583`, `rule.21.RaceStepStream-Yield-Resumable@L80603`, `rule.21.ResumeRaceState-Step@L80623`
- `rule.21.ResumeRaceState-Done@L80642`, `rule.21.EvalSigma-Race-Stream-Resume@L80659`, `requirement.21.StreamingRaceResumptionOrder@L80679`, `requirement.21.AllRuntimeSemantics@L80692`, `def.21.AllStateAndInitSignature@L80712`, `rule.21.InitAll@L80728`, `def.21.AllStepSignature@L80747`, `rule.21.AllStep-Complete@L80762`
- `rule.21.AllStep-Failed@L80781`, `rule.21.AllStep-Resume@L80801`, `def.21.AllLoopSignature@L80821`, `rule.21.AllLoop-AllCompleted@L80836`, `rule.21.AllLoop-Failed@L80854`, `rule.21.AllLoop-Continue@L80872`, `rule.21.EvalSigma-All@L80891`, `requirement.21.UntilRuntimeSemantics@L80909`
- `def.21.AsyncCombinatorRuntimeWrappers@L80926`, `rule.21.EvalSigma-Map-Create@L80949`, `rule.21.EvalSigma-Map-Resume-Yield@L80967`, `rule.21.EvalSigma-Map-Resume-Complete@L80985`, `rule.21.EvalSigma-Map-Resume-Failed@L81003`, `rule.21.EvalSigma-Filter-Create@L81021`, `rule.21.EvalSigma-Filter-Resume-Pass@L81039`, `rule.21.EvalSigma-Filter-Resume-Skip@L81057`
- `rule.21.EvalSigma-Filter-Resume-Complete@L81076`, `rule.21.EvalSigma-Take-Create@L81094`, `rule.21.EvalSigma-Take-Resume-Yield@L81112`, `rule.21.EvalSigma-Take-Resume-Done@L81130`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81148`, `rule.21.EvalSigma-Fold-Create@L81166`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81184`, `rule.21.EvalSigma-Fold-Resume-Complete@L81202`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81220`, `rule.21.EvalSigma-Chain-Create@L81238`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81256`, `rule.21.EvalSigma-Chain-Resume-Chained@L81274`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81292`, `def.21.AsyncComposeIR@L81312`, `rule.21.Lower-Expr-Sync@L81325`, `requirement.21.SyncLoopIRSemantics@L81343`
- `rule.21.Lower-Expr-Race-Return@L81356`, `rule.21.Lower-Expr-Race-Stream@L81374`, `requirement.21.RaceInitIRSemantics@L81392`, `requirement.21.RaceResumeIRSemantics@L81408`, `rule.21.Lower-Expr-All@L81421`, `requirement.21.AllJoinIRSemantics@L81437`, `requirement.21.AsyncCombinatorWrapperLowering@L81450`, `rule.21.Lower-Async-Map@L81463`
- `rule.21.Lower-Async-Filter@L81479`, `rule.21.Lower-Async-Take@L81495`, `rule.21.Lower-Async-Fold@L81511`, `rule.21.Lower-Async-Chain@L81527`, `requirement.21.AsyncWrapperLoweringSemantics@L81543`, `diagnostics.21.AsyncCompositionDiagnostics@L81558`, `requirement.21.AsyncStateMachineSyntaxSurface@L81588`, `def.21.AsyncProcedureDefinition@L81601`
- `requirement.21.AsyncStateMachineParsingSurface@L81616`, `def.21.AsyncStateMachineHelperForms@L81633`, `requirement.21.AsyncFrameStoredState@L81662`, `def.21.LiveAcrossSuspension@L81681`, `rule.21.Warn-Async-LargeCapture@L81696`, `rule.21.Warn-Async-LargeCapture-Ok@L81714`, `requirement.21.AsyncLargeCaptureWarningEmission@L81732`, `rule.21.Async-Capture-Err@L81745`
- `rule.21.P-Async-Create@L81763`, `rule.21.Prov-Async-Escape-Err@L81782`, `requirement.21.AsyncErrorPropagationTypingReference@L81801`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81816`, `requirement.21.AsyncSettlementRuntimeSemantics@L81834`, `requirement.21.AsyncResumeRuntimeSemantics@L81851`, `requirement.21.AsyncFailureRuntimeSemantics@L81867`, `def.21.AsyncStateMachineLoweringJudgements@L81887`
- `def.21.AsyncStateMachineFrameHelpers@L81901`, `rule.21.Lower-Async-Proc@L81917`, `requirement.21.AsyncFrameInitIRSemantics@L81937`, `rule.21.Lower-Async-Resume@L81956`, `requirement.21.AsyncResumeSwitchIRSemantics@L81975`, `rule.21.Lower-Async-Suspend@L81989`, `rule.21.Lower-Async-Complete@L82008`, `rule.21.Lower-Async-Fail@L82027`
- `requirement.21.AsyncFailStateIRSemantics@L82046`, `diagnostics.21.AsyncStateMachineDiagnostics@L82062`, `requirement.21.AsyncKeySyntaxSurface@L82083`, `requirement.21.AsyncKeyParsingSurface@L82098`, `def.21.AsyncKeyExistingAstForms@L82115`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82131`, `requirement.21.AsyncKeyRestrictions@L82148`, `rule.21.A-Closure-Yield-Keys-Err@L82165`
- `requirement.21.SharedCapturingClosureYieldKeys@L82184`, `requirement.21.YieldReleaseStalenessWarning@L82197`, `requirements.21.AsyncCapabilityRequirements@L82212`, `requirement.21.AsyncSuspensionAccessRights@L82232`, `requirement.21.YieldReleaseRuntimeReference@L82245`, `requirement.21.AsyncKeyFailureHandlingReference@L82258`, `def.21.AsyncKeyIR@L82273`, `rule.21.Lower-Wait-Key-Illegal@L82286`
- `rule.21.Lower-Yield-Release-Keys@L82304`, `rule.21.Lower-YieldFrom-Release-Keys@L82322`, `rule.21.Lower-Closure-Yield-Shared@L82340`, `requirement.21.StaleValueMarkIRDiagnostics@L82358`, `diagnostics.21.AsyncKeyDiagnostics@L82373`, `diagnostics.21.AsyncDiagnosticsSupplement@L82392`
- `requirement.21.AsyncTypeNoAdditionalConcreteGrammar@L77722`, `requirement.21.ReservedAsyncTypeConstructors@L77735`, `requirement.21.AsyncParameterDefaults@L77755`, `requirement.21.ReservedAsyncStates@L77768`, `parse.21.AsyncTypes@L77787`, `parse.21.UnappliedAsyncPath@L77802`, `ast.21.AsyncModalDeclaration@L77817`, `ast.21.AsyncAliases@L77888`
- `ast.21.AsyncCombinatorMembers@L77909`, `def.21.AsyncSigAndBodyReturnType@L77929`, `rule.21.Sub-Async@L77956`, `rule.21.WF-Async@L77977`, `rule.21.WF-Async-ArgCount-Err@L77995`, `rule.21.WF-Async-Arg-WF-Err@L78013`, `rule.21.WF-Async-Path-Err@L78031`, `requirement.21.AsyncFailedUninhabitedForNeverError@L78049`
- `requirement.21.AsyncTypeDynamicSemanticsReference@L78064`, `def.21.AsyncTypeLoweringForms@L78081`, `requirement.21.AsyncNeverErrorLowering@L78112`, `rule.21.Lower-Async-Type@L78125`, `rule.21.Lower-Async-Alias@L78145`, `diagnostics.21.AsyncType@L78165`, `grammar.21.SuspensionForms@L78184`, `parse.21.SuspensionFormsPrimaryExpressions@L78203`
- `rule.21.Parse-Wait-Expr@L78218`, `rule.21.Parse-Yield-From-Expr@L78238`, `rule.21.Parse-Yield-Expr@L78261`, `ast.21.SuspensionForms@L78284`, `ast.21.SuspensionFormResolution@L78304`, `ast.21.SuspensionFormEvaluationOrder@L78323`, `rule.21.T-Wait@L78346`, `rule.21.T-Wait-Future@L78364`
- `rule.21.Wait-Handle-Err@L78382`, `rule.21.T-Yield@L78402`, `rule.21.Yield-NotAsync-Err@L78420`, `rule.21.Yield-Out-Err@L78438`, `rule.21.T-Yield-From@L78458`, `rule.21.YieldFrom-NotAsync-Err@L78476`, `rule.21.YieldFrom-Out-Err@L78494`, `rule.21.YieldFrom-In-Err@L78513`
- `rule.21.YieldFrom-ErrType-Err@L78531`, `requirement.21.SuspensionKeyRestrictionsReference@L78549`, `requirement.21.WaitRuntimeSemantics@L78564`, `def.21.WaitRuntimeHelpers@L78586`, `rule.21.EvalSigma-Wait-Spawned-Ready@L78610`, `rule.21.EvalSigma-Wait-Spawned-Pending@L78628`, `requirement.21.FailedSpawnedWaitHandledByParallelPanic@L78647`, `rule.21.EvalSigma-Wait-Tracked-Ready@L78660`
- `rule.21.EvalSigma-Wait-Tracked-Pending@L78678`, `rule.21.EvalSigma-Wait-Ctrl@L78697`, `requirement.21.YieldRuntimeSemantics@L78715`, `def.21.ResumptionHelpers@L78735`, `rule.21.EvalSigma-Yield@L78770`, `rule.21.EvalSigma-Yield-Release@L78789`, `rule.21.EvalSigma-Yield-Resume@L78808`, `requirement.21.YieldFromRuntimeSemantics@L78827`
- `rule.21.EvalSigma-YieldFrom-Suspended@L78847`, `rule.21.EvalSigma-YieldFrom-Completed@L78867`, `rule.21.EvalSigma-YieldFrom-Failed@L78885`, `rule.21.EvalSigma-YieldFrom-Resume@L78903`, `def.21.EvalYieldFromContinueSignature@L78922`, `rule.21.EvalYieldFromContinue-Suspended@L78937`, `rule.21.EvalYieldFromContinue-Completed@L78957`, `rule.21.EvalYieldFromContinue-Failed@L78975`
- `def.21.SuspensionLoweringForms@L78995`, `rule.21.Lower-Wait-Spawned@L79016`, `rule.21.Lower-Wait-Tracked@L79034`, `rule.21.Lower-Yield@L79052`, `rule.21.Lower-Yield-Release@L79070`, `requirement.21.YieldReleaseReacquireLowering@L79088`, `rule.21.Lower-YieldFrom@L79101`, `requirement.21.YieldFromEnterLoweringLoop@L79119`
- `diagnostics.21.SuspensionForms@L79137`, `requirement.21.AsyncIterationSyntax@L79162`, `grammar.21.CompositionForms@L79177`, `requirement.21.AsyncMethodCallSurfaces@L79196`, `requirement.21.UntilMethodCallSurface@L79216`, `parse.21.CompositionPrimaryExpressions@L79231`, `rule.21.Parse-Sync-Expr@L79246`, `rule.21.Parse-Race-Expr@L79266`
- `rule.21.Parse-RaceArms-Cons@L79286`, `rule.21.Parse-RaceArm@L79304`, `rule.21.Parse-RaceArmsTail-End@L79324`, `rule.21.Parse-RaceArmsTail-TrailingComma@L79342`, `rule.21.Parse-RaceArmsTail-Comma@L79360`, `rule.21.Parse-RaceHandler-Yield@L79378`, `rule.21.Parse-RaceHandler-Return@L79396`, `rule.21.Parse-All-Expr@L79416`
- `parse.21.CompositionOrdinarySurfaces@L79434`, `ast.21.CompositionForms@L79449`, `ast.21.AsyncIterationLoopForm@L79471`, `ast.21.CompositionMethodCallForms@L79488`, `ast.21.CompositionResolution@L79503`, `ast.21.CompositionEvaluationOrder@L79532`, `rule.21.T-Loop-Iter-Async@L79558`, `rule.21.Loop-Async-Err@L79580`
- `requirement.21.ManualSteppingRequirement@L79598`, `def.21.SyncYieldContainment@L79613`, `rule.21.Sync-Yield-Err@L79629`, `rule.21.Sync-YieldFrom-Err@L79647`, `rule.21.T-Sync@L79665`, `rule.21.Sync-Async-Context-Err@L79683`, `rule.21.Sync-Out-Err@L79701`, `rule.21.Sync-In-Err@L79720`
- `def.21.RaceMode@L79740`, `rule.21.T-Race@L79758`, `rule.21.T-Race-Stream@L79779`, `rule.21.Race-Arity-Err@L79800`, `rule.21.Race-Handler-Mix-Err@L79818`, `rule.21.Race-Operand-Out-Err@L79836`, `rule.21.Race-Operand-Err@L79855`, `rule.21.Race-Stream-Operand-Err@L79874`
- `rule.21.Race-Handler-Type-Err@L79893`, `rule.21.Race-Stream-Handler-Type-Err@L79914`, `rule.21.T-All@L79937`, `rule.21.All-Out-Err@L79956`, `rule.21.All-In-Err@L79974`, `def.21.UntilType@L79992`, `def.21.AsyncCombinatorTypes@L80009`, `requirement.21.AsyncCombinatorMemberLookup@L80030`
- `rule.21.T-Async-Map@L80045`, `rule.21.T-Async-Filter@L80063`, `rule.21.T-Async-Take@L80081`, `rule.21.T-Async-Fold@L80099`, `rule.21.T-Async-Chain@L80117`, `requirement.21.AsyncIterationRuntimeSemantics@L80137`, `requirement.21.ManualSteppingRuntimeSemantics@L80155`, `requirement.21.SyncRuntimeSemantics@L80168`
- `def.21.SyncStepSignature@L80188`, `rule.21.SyncStep-Suspended@L80203`, `rule.21.EvalSigma-Sync-Suspended@L80221`, `rule.21.EvalSigma-Sync-Completed@L80240`, `rule.21.EvalSigma-Sync-Failed@L80258`, `requirement.21.RaceReturnRuntimeSemantics@L80276`, `requirement.21.RaceStreamingRuntimeSemantics@L80294`, `def.21.RaceSelectionAndState@L80315`
- `rule.21.InitRace@L80345`, `def.21.RaceStepReturnSignature@L80364`, `rule.21.RaceStepReturn-Completed@L80379`, `rule.21.RaceStepReturn-Failed@L80399`, `rule.21.RaceStepReturn-Continue@L80418`, `rule.21.EvalSigma-Race-Return@L80438`, `def.21.RaceStepStreamSignature@L80457`, `rule.21.RaceStepStream-Yield-Initial@L80472`
- `rule.21.RaceStepStream-AllComplete@L80492`, `rule.21.RaceStepStream-Failed@L80510`, `rule.21.EvalSigma-Race-Stream@L80529`, `def.21.CancelAllSignature@L80548`, `rule.21.CancelAll@L80563`, `def.21.RaceStreamSuspensionState@L80583`, `rule.21.RaceStepStream-Yield-Resumable@L80603`, `rule.21.ResumeRaceState-Step@L80623`
- `rule.21.ResumeRaceState-Done@L80642`, `rule.21.EvalSigma-Race-Stream-Resume@L80659`, `requirement.21.StreamingRaceResumptionOrder@L80679`, `requirement.21.AllRuntimeSemantics@L80692`, `def.21.AllStateAndInitSignature@L80712`, `rule.21.InitAll@L80728`, `def.21.AllStepSignature@L80747`, `rule.21.AllStep-Complete@L80762`
- `rule.21.AllStep-Failed@L80781`, `rule.21.AllStep-Resume@L80801`, `def.21.AllLoopSignature@L80821`, `rule.21.AllLoop-AllCompleted@L80836`, `rule.21.AllLoop-Failed@L80854`, `rule.21.AllLoop-Continue@L80872`, `rule.21.EvalSigma-All@L80891`, `requirement.21.UntilRuntimeSemantics@L80909`
- `def.21.AsyncCombinatorRuntimeWrappers@L80926`, `rule.21.EvalSigma-Map-Create@L80949`, `rule.21.EvalSigma-Map-Resume-Yield@L80967`, `rule.21.EvalSigma-Map-Resume-Complete@L80985`, `rule.21.EvalSigma-Map-Resume-Failed@L81003`, `rule.21.EvalSigma-Filter-Create@L81021`, `rule.21.EvalSigma-Filter-Resume-Pass@L81039`, `rule.21.EvalSigma-Filter-Resume-Skip@L81057`
- `rule.21.EvalSigma-Filter-Resume-Complete@L81076`, `rule.21.EvalSigma-Take-Create@L81094`, `rule.21.EvalSigma-Take-Resume-Yield@L81112`, `rule.21.EvalSigma-Take-Resume-Done@L81130`, `rule.21.EvalSigma-Take-Resume-Source-Complete@L81148`, `rule.21.EvalSigma-Fold-Create@L81166`, `rule.21.EvalSigma-Fold-Resume-Accumulate@L81184`, `rule.21.EvalSigma-Fold-Resume-Complete@L81202`
- `rule.21.EvalSigma-Fold-Resume-Failed@L81220`, `rule.21.EvalSigma-Chain-Create@L81238`, `rule.21.EvalSigma-Chain-Resume-Source-Complete@L81256`, `rule.21.EvalSigma-Chain-Resume-Chained@L81274`, `rule.21.EvalSigma-Chain-Resume-Source-Failed@L81292`, `def.21.AsyncComposeIR@L81312`, `rule.21.Lower-Expr-Sync@L81325`, `requirement.21.SyncLoopIRSemantics@L81343`
- `rule.21.Lower-Expr-Race-Return@L81356`, `rule.21.Lower-Expr-Race-Stream@L81374`, `requirement.21.RaceInitIRSemantics@L81392`, `requirement.21.RaceResumeIRSemantics@L81408`, `rule.21.Lower-Expr-All@L81421`, `requirement.21.AllJoinIRSemantics@L81437`, `requirement.21.AsyncCombinatorWrapperLowering@L81450`, `rule.21.Lower-Async-Map@L81463`
- `rule.21.Lower-Async-Filter@L81479`, `rule.21.Lower-Async-Take@L81495`, `rule.21.Lower-Async-Fold@L81511`, `rule.21.Lower-Async-Chain@L81527`, `requirement.21.AsyncWrapperLoweringSemantics@L81543`, `diagnostics.21.AsyncCompositionDiagnostics@L81558`, `requirement.21.AsyncStateMachineSyntaxSurface@L81588`, `def.21.AsyncProcedureDefinition@L81601`
- `requirement.21.AsyncStateMachineParsingSurface@L81616`, `def.21.AsyncStateMachineHelperForms@L81633`, `requirement.21.AsyncFrameStoredState@L81662`, `def.21.LiveAcrossSuspension@L81681`, `rule.21.Warn-Async-LargeCapture@L81696`, `rule.21.Warn-Async-LargeCapture-Ok@L81714`, `requirement.21.AsyncLargeCaptureWarningEmission@L81732`, `rule.21.Async-Capture-Err@L81745`
- `rule.21.P-Async-Create@L81763`, `rule.21.Prov-Async-Escape-Err@L81782`, `requirement.21.AsyncErrorPropagationTypingReference@L81801`, `requirement.21.AsyncProcedureCallRuntimeSemantics@L81816`, `requirement.21.AsyncSettlementRuntimeSemantics@L81834`, `requirement.21.AsyncResumeRuntimeSemantics@L81851`, `requirement.21.AsyncFailureRuntimeSemantics@L81867`, `def.21.AsyncStateMachineLoweringJudgements@L81887`
- `def.21.AsyncStateMachineFrameHelpers@L81901`, `rule.21.Lower-Async-Proc@L81917`, `requirement.21.AsyncFrameInitIRSemantics@L81937`, `rule.21.Lower-Async-Resume@L81956`, `requirement.21.AsyncResumeSwitchIRSemantics@L81975`, `rule.21.Lower-Async-Suspend@L81989`, `rule.21.Lower-Async-Complete@L82008`, `rule.21.Lower-Async-Fail@L82027`
- `requirement.21.AsyncFailStateIRSemantics@L82046`, `diagnostics.21.AsyncStateMachineDiagnostics@L82062`, `requirement.21.AsyncKeySyntaxSurface@L82083`, `requirement.21.AsyncKeyParsingSurface@L82098`, `def.21.AsyncKeyExistingAstForms@L82115`, `requirement.21.AsyncKeyNoAdditionalAstVariants@L82131`, `requirement.21.AsyncKeyRestrictions@L82148`, `rule.21.A-Closure-Yield-Keys-Err@L82165`
- `requirement.21.SharedCapturingClosureYieldKeys@L82184`, `requirement.21.YieldReleaseStalenessWarning@L82197`, `requirements.21.AsyncCapabilityRequirements@L82212`, `requirement.21.AsyncSuspensionAccessRights@L82232`, `requirement.21.YieldReleaseRuntimeReference@L82245`, `requirement.21.AsyncKeyFailureHandlingReference@L82258`, `def.21.AsyncKeyIR@L82273`, `rule.21.Lower-Wait-Key-Illegal@L82286`
- `rule.21.Lower-Yield-Release-Keys@L82304`, `rule.21.Lower-YieldFrom-Release-Keys@L82322`, `rule.21.Lower-Closure-Yield-Shared@L82340`, `requirement.21.StaleValueMarkIRDiagnostics@L82358`, `diagnostics.21.AsyncKeyDiagnostics@L82373`, `diagnostics.21.AsyncDiagnosticsSupplement@L82392`

#### `spec.comptime`

Count: 181 total; 181 required; 0 recommended; 0 informative. Ledger line span: L82027-L85037.

- `requirement.22.Phase2ExecutionPosition@L82412`, `grammar.22.CompileTimeForms@L82429`, `def.22.CtParseJudg@L82451`, `rule.22.Parse-CtProc@L82464`, `rule.22.Parse-CtStmt@L82480`, `rule.22.Parse-CtExpr@L82496`, `rule.22.Parse-CtIf@L82512`, `rule.22.Parse-CtLoopIter@L82528`
- `rule.22.Parse-CtElseOpt-None@L82544`, `rule.22.Parse-CtElseOpt-Block@L82560`, `rule.22.Parse-CtElseOpt-ElseIf@L82576`, `def.22.CtNodeForms@L82594`, `def.22.CtExecutionState@L82613`, `def.22.CompileTimeJudgementSets@L82641`, `def.22.CtValueForms@L82660`, `def.22.CompileTimeTypingEnvironment@L82681`
- `def.22.CtAvailabilityAndForbiddenTypes@L82694`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82727`, `requirement.22.CompileTimeProhibitedConstructs@L82740`, `rule.22.T-CtStmt@L82758`, `rule.22.T-CtExpr@L82774`, `rule.22.T-CtIf@L82790`, `rule.22.T-CtLoopIter@L82806`, `rule.22.T-CtProc@L82822`
- `requirement.22.CompileTimeProcedureContracts@L82838`, `requirement.22.CompileTimeProcedureContextRestriction@L82851`, `requirement.22.ComptimeIfSelectedBranchOnly@L82864`, `requirement.22.ComptimeLoopIterationSemantics@L82877`, `def.22.Phase2ModuleOrder@L82893`, `def.22.CtDynamicHelpers@L82906`, `requirement.22.ComptimePassExecutionRequirements@L82928`, `requirement.22.CtEvalOrdinarySemantics@L82947`
- `requirement.22.CtExpandOrdinaryTraversal@L82960`, `rule.22.ComptimePass-Empty@L82973`, `rule.22.ComptimePass-Cons@L82988`, `rule.22.ComptimePass@L83004`, `rule.22.CtExecModule@L83022`, `rule.22.CtExpandItemSeq-Empty@L83038`, `rule.22.CtExpandItemSeq-Cons@L83053`, `def.22.CtExpandItemResult@L83069`
- `requirement.22.CtPendingEmitsTransfer@L83082`, `rule.22.CtExpandItem-CtProc@L83095`, `rule.22.CtExpandStmtSeq-Empty@L83111`, `rule.22.CtExpandStmtSeq-Cons@L83126`, `rule.22.CtExpandBlock@L83142`, `rule.22.CtExpandStmt-CtStmt@L83158`, `rule.22.CtExpandExpr-CtExpr@L83174`, `rule.22.CtExpandExpr-CtIf-True@L83190`
- `rule.22.CtExpandExpr-CtIf-False@L83206`, `rule.22.CtExpandExpr-CtLoopIter@L83222`, `rule.22.CtLoopIterUnroll-Empty@L83238`, `rule.22.CtLoopIterUnroll-Cons@L83253`, `def.22.CtLiteralize@L83269`, `requirement.22.CompileTimeFormsLowering@L83293`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83313`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83330`
- `def.22.CtCapName@L83345`, `rule.22.Parse-CtCapRef@L83358`, `requirement.22.CtCapMethodCallParsing@L83374`, `def.22.CtCapabilitiesAndBuiltinTypes@L83389`, `def.22.CtReflectionInfoFields@L83412`, `def.22.CtValueConversionHelpers@L83428`, `def.22.TypeEmitterInterface@L83458`, `def.22.IntrospectInterface@L83474`
- `def.22.ProjectFilesInterface@L83496`, `def.22.ComptimeDiagnosticsInterface@L83516`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83538`, `requirement.22.TypeEmitterAvailability@L83551`, `requirement.22.ProjectFilesAvailability@L83566`, `def.22.CtCapBindings@L83579`, `requirement.22.ProjectFilesPathRestrictions@L83592`, `requirement.22.TypeEmitterEmitTypeRequirement@L83609`
- `def.22.CtCapabilityDynamicHelpers@L83624`, `rule.22.CtBuiltin-Emit@L83647`, `rule.22.CtBuiltin-ProjectRoot@L83663`, `rule.22.CtBuiltin-Read@L83679`, `rule.22.CtBuiltin-Read-InvalidPath@L83695`, `rule.22.CtBuiltin-ReadBytes@L83711`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83727`, `rule.22.CtBuiltin-Exists@L83743`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83759`, `rule.22.CtBuiltin-ListDir@L83775`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83791`, `rule.22.CtBuiltin-Diagnostics-Error@L83807`, `rule.22.CtBuiltin-Diagnostics-Warning@L83823`, `rule.22.CtBuiltin-Diagnostics-Note@L83839`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83855`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83871`
- `requirement.22.ProjectFileSnapshotStability@L83887`, `requirement.22.CompileTimeCapabilitiesLowering@L83902`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83917`, `grammar.22.TypeLiteral@L83934`, `def.22.ReflectParseJudg@L83951`, `rule.22.Parse-TypeLiteral@L83964`, `def.22.Reflectable@L83982`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L84007`
- `def.22.TypeCategory@L84021`, `def.22.ReflectFields@L84062`, `def.22.ReflectVariants@L84079`, `def.22.ReflectStates@L84096`, `def.22.ReflectionPayloadAndModuleHelpers@L84114`, `rule.22.T-TypeLiteral@L84139`, `requirement.22.IntrospectCategoryValidity@L84155`, `requirement.22.IntrospectMemberValidity@L84168`
- `requirement.22.ReflectionCanonicalOrder@L84183`, `requirement.22.IntrospectImplementsFormSemantics@L84199`, `rule.22.CtEval-TypeLiteral@L84214`, `rule.22.CtBuiltin-Reflect-Category@L84230`, `rule.22.CtBuiltin-Reflect-Fields@L84246`, `rule.22.CtBuiltin-Reflect-Variants@L84262`, `rule.22.CtBuiltin-Reflect-States@L84278`, `rule.22.CtBuiltin-Reflect-Form@L84294`
- `rule.22.CtBuiltin-Reflect-TypeName@L84310`, `rule.22.CtBuiltin-Reflect-ModulePath@L84326`, `requirement.22.ReflectionPurityAndImmutability@L84342`, `requirement.22.ReflectionLowering@L84357`, `diagnostics.22.ReflectionDiagnosticsReference@L84372`, `grammar.22.QuoteSpliceEmission@L84389`, `def.22.QuoteParseJudg@L84411`, `def.22.CaptureQuotedTokens@L84424`
- `rule.22.Parse-Quote-Raw@L84437`, `rule.22.Parse-Quote-Type@L84453`, `rule.22.Parse-Quote-Pattern@L84469`, `def.22.AstForms@L84487`, `def.22.QuoteSpliceHygieneForms@L84506`, `def.22.QuoteJudg@L84523`, `def.22.ExpectedAstKind@L84536`, `def.22.CtLiteralType@L84554`
- `def.22.SpliceCompat@L84575`, `requirement.22.QuoteCompileTimeOnly@L84595`, `def.22.ResolveQuoteKind@L84608`, `requirement.22.QuotedContentValidity@L84623`, `requirement.22.SpliceContextAndTypeCompatibility@L84636`, `requirement.22.SpliceIdentifierPositionRestrictions@L84649`, `requirement.22.StringSpliceIdentifierHygiene@L84662`, `requirement.22.EmitterEmitWellFormedness@L84675`
- `def.22.ParseQuotedBody@L84690`, `def.22.RenderSplice@L84707`, `requirement.22.HygienizeAstProperties@L84725`, `requirement.22.HygienicInternalReferences@L84741`, `requirement.22.ImportUsingHygiene@L84754`, `rule.22.CtEval-Quote@L84767`, `requirement.22.QuoteBuildSpliceOrder@L84783`, `requirement.22.EmissionOrder@L84797`
- `requirement.22.QuoteSpliceEmissionLowering@L84814`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84829`, `grammar.22.DeriveTargetsAndContracts@L84846`, `def.22.DeriveParseJudg@L84867`, `requirement.22.DeriveAttributeParsingReference@L84880`, `rule.22.Parse-DeriveTargetDecl@L84893`, `rule.22.Parse-DeriveContractOpt-None@L84909`, `rule.22.Parse-DeriveContractOpt-Yes@L84925`
- `rule.22.Parse-DeriveClauseList-Cons@L84941`, `rule.22.Parse-DeriveClause-Requires@L84957`, `rule.22.Parse-DeriveClause-Emits@L84973`, `rule.22.Parse-DeriveClauseTail-End@L84989`, `rule.22.Parse-DeriveClauseTail-Comma@L85005`, `def.22.DeriveTargetDecl@L85023`, `def.22.DeriveGraphAndOrder@L85038`, `requirement.22.DeriveAttributeTargetKinds@L85062`
- `requirement.22.DeriveTargetNameResolution@L85075`, `requirement.22.DeriveTargetBodyBindings@L85088`, `requirement.22.DeriveTargetBodyRestrictions@L85105`, `requirement.22.DeriveExecutionOrder@L85118`, `requirement.22.DeriveOrderTieBreaker@L85133`, `requirement.22.DeriveRequiresValidation@L85146`, `requirement.22.DeriveEmitsValidation@L85159`, `requirement.22.DeriveRequiresEmitsScope@L85172`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85187`, `rule.22.CtExpandItem-DeriveTargetDecl@L85200`, `rule.22.RunDeriveSet-Empty@L85216`, `rule.22.RunDeriveSet-Cons@L85231`, `rule.22.RunDeriveTarget@L85247`, `def.22.BindDeriveTargetInputs@L85263`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85276`, `requirement.22.DeriveTargetExecutionTiming@L85292`
- `requirement.22.DeriveTargetFailureSemantics@L85305`, `requirement.22.DeriveTargetsLowering@L85320`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85335`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85350`, `requirement.22.UserDiagnosticBuiltinEmission@L85426`
- `requirement.22.Phase2ExecutionPosition@L82412`, `grammar.22.CompileTimeForms@L82429`, `def.22.CtParseJudg@L82451`, `rule.22.Parse-CtProc@L82464`, `rule.22.Parse-CtStmt@L82480`, `rule.22.Parse-CtExpr@L82496`, `rule.22.Parse-CtIf@L82512`, `rule.22.Parse-CtLoopIter@L82528`
- `rule.22.Parse-CtElseOpt-None@L82544`, `rule.22.Parse-CtElseOpt-Block@L82560`, `rule.22.Parse-CtElseOpt-ElseIf@L82576`, `def.22.CtNodeForms@L82594`, `def.22.CtExecutionState@L82613`, `def.22.CompileTimeJudgementSets@L82641`, `def.22.CtValueForms@L82660`, `def.22.CompileTimeTypingEnvironment@L82681`
- `def.22.CtAvailabilityAndForbiddenTypes@L82694`, `requirement.22.CompileTimeTypeAvailabilityRejection@L82727`, `requirement.22.CompileTimeProhibitedConstructs@L82740`, `rule.22.T-CtStmt@L82758`, `rule.22.T-CtExpr@L82774`, `rule.22.T-CtIf@L82790`, `rule.22.T-CtLoopIter@L82806`, `rule.22.T-CtProc@L82822`
- `requirement.22.CompileTimeProcedureContracts@L82838`, `requirement.22.CompileTimeProcedureContextRestriction@L82851`, `requirement.22.ComptimeIfSelectedBranchOnly@L82864`, `requirement.22.ComptimeLoopIterationSemantics@L82877`, `def.22.Phase2ModuleOrder@L82893`, `def.22.CtDynamicHelpers@L82906`, `requirement.22.ComptimePassExecutionRequirements@L82928`, `requirement.22.CtEvalOrdinarySemantics@L82947`
- `requirement.22.CtExpandOrdinaryTraversal@L82960`, `rule.22.ComptimePass-Empty@L82973`, `rule.22.ComptimePass-Cons@L82988`, `rule.22.ComptimePass@L83004`, `rule.22.CtExecModule@L83022`, `rule.22.CtExpandItemSeq-Empty@L83038`, `rule.22.CtExpandItemSeq-Cons@L83053`, `def.22.CtExpandItemResult@L83069`
- `requirement.22.CtPendingEmitsTransfer@L83082`, `rule.22.CtExpandItem-CtProc@L83095`, `rule.22.CtExpandStmtSeq-Empty@L83111`, `rule.22.CtExpandStmtSeq-Cons@L83126`, `rule.22.CtExpandBlock@L83142`, `rule.22.CtExpandStmt-CtStmt@L83158`, `rule.22.CtExpandExpr-CtExpr@L83174`, `rule.22.CtExpandExpr-CtIf-True@L83190`
- `rule.22.CtExpandExpr-CtIf-False@L83206`, `rule.22.CtExpandExpr-CtLoopIter@L83222`, `rule.22.CtLoopIterUnroll-Empty@L83238`, `rule.22.CtLoopIterUnroll-Cons@L83253`, `def.22.CtLiteralize@L83269`, `requirement.22.CompileTimeFormsLowering@L83293`, `diagnostics.22.CompileTimeFormsDiagnosticsReference@L83313`, `requirement.22.CompileTimeCapabilitiesSyntaxSurface@L83330`
- `def.22.CtCapName@L83345`, `rule.22.Parse-CtCapRef@L83358`, `requirement.22.CtCapMethodCallParsing@L83374`, `def.22.CtCapabilitiesAndBuiltinTypes@L83389`, `def.22.CtReflectionInfoFields@L83412`, `def.22.CtValueConversionHelpers@L83428`, `def.22.TypeEmitterInterface@L83458`, `def.22.IntrospectInterface@L83474`
- `def.22.ProjectFilesInterface@L83496`, `def.22.ComptimeDiagnosticsInterface@L83516`, `requirement.22.IntrospectAndDiagnosticsAvailability@L83538`, `requirement.22.TypeEmitterAvailability@L83551`, `requirement.22.ProjectFilesAvailability@L83566`, `def.22.CtCapBindings@L83579`, `requirement.22.ProjectFilesPathRestrictions@L83592`, `requirement.22.TypeEmitterEmitTypeRequirement@L83609`
- `def.22.CtCapabilityDynamicHelpers@L83624`, `rule.22.CtBuiltin-Emit@L83647`, `rule.22.CtBuiltin-ProjectRoot@L83663`, `rule.22.CtBuiltin-Read@L83679`, `rule.22.CtBuiltin-Read-InvalidPath@L83695`, `rule.22.CtBuiltin-ReadBytes@L83711`, `rule.22.CtBuiltin-ReadBytes-InvalidPath@L83727`, `rule.22.CtBuiltin-Exists@L83743`
- `rule.22.CtBuiltin-Exists-InvalidPath@L83759`, `rule.22.CtBuiltin-ListDir@L83775`, `rule.22.CtBuiltin-ListDir-InvalidPath@L83791`, `rule.22.CtBuiltin-Diagnostics-Error@L83807`, `rule.22.CtBuiltin-Diagnostics-Warning@L83823`, `rule.22.CtBuiltin-Diagnostics-Note@L83839`, `rule.22.CtBuiltin-Diagnostics-CurrentSpan@L83855`, `rule.22.CtBuiltin-Diagnostics-CurrentModule@L83871`
- `requirement.22.ProjectFileSnapshotStability@L83887`, `requirement.22.CompileTimeCapabilitiesLowering@L83902`, `diagnostics.22.CompileTimeCapabilitiesDiagnosticsReference@L83917`, `grammar.22.TypeLiteral@L83934`, `def.22.ReflectParseJudg@L83951`, `rule.22.Parse-TypeLiteral@L83964`, `def.22.Reflectable@L83982`, `def.22.ReflectJudgementsAndTypeLiteralExpr@L84007`
- `def.22.TypeCategory@L84021`, `def.22.ReflectFields@L84062`, `def.22.ReflectVariants@L84079`, `def.22.ReflectStates@L84096`, `def.22.ReflectionPayloadAndModuleHelpers@L84114`, `rule.22.T-TypeLiteral@L84139`, `requirement.22.IntrospectCategoryValidity@L84155`, `requirement.22.IntrospectMemberValidity@L84168`
- `requirement.22.ReflectionCanonicalOrder@L84183`, `requirement.22.IntrospectImplementsFormSemantics@L84199`, `rule.22.CtEval-TypeLiteral@L84214`, `rule.22.CtBuiltin-Reflect-Category@L84230`, `rule.22.CtBuiltin-Reflect-Fields@L84246`, `rule.22.CtBuiltin-Reflect-Variants@L84262`, `rule.22.CtBuiltin-Reflect-States@L84278`, `rule.22.CtBuiltin-Reflect-Form@L84294`
- `rule.22.CtBuiltin-Reflect-TypeName@L84310`, `rule.22.CtBuiltin-Reflect-ModulePath@L84326`, `requirement.22.ReflectionPurityAndImmutability@L84342`, `requirement.22.ReflectionLowering@L84357`, `diagnostics.22.ReflectionDiagnosticsReference@L84372`, `grammar.22.QuoteSpliceEmission@L84389`, `def.22.QuoteParseJudg@L84411`, `def.22.CaptureQuotedTokens@L84424`
- `rule.22.Parse-Quote-Raw@L84437`, `rule.22.Parse-Quote-Type@L84453`, `rule.22.Parse-Quote-Pattern@L84469`, `def.22.AstForms@L84487`, `def.22.QuoteSpliceHygieneForms@L84506`, `def.22.QuoteJudg@L84523`, `def.22.ExpectedAstKind@L84536`, `def.22.CtLiteralType@L84554`
- `def.22.SpliceCompat@L84575`, `requirement.22.QuoteCompileTimeOnly@L84595`, `def.22.ResolveQuoteKind@L84608`, `requirement.22.QuotedContentValidity@L84623`, `requirement.22.SpliceContextAndTypeCompatibility@L84636`, `requirement.22.SpliceIdentifierPositionRestrictions@L84649`, `requirement.22.StringSpliceIdentifierHygiene@L84662`, `requirement.22.EmitterEmitWellFormedness@L84675`
- `def.22.ParseQuotedBody@L84690`, `def.22.RenderSplice@L84707`, `requirement.22.HygienizeAstProperties@L84725`, `requirement.22.HygienicInternalReferences@L84741`, `requirement.22.ImportUsingHygiene@L84754`, `rule.22.CtEval-Quote@L84767`, `requirement.22.QuoteBuildSpliceOrder@L84783`, `requirement.22.EmissionOrder@L84797`
- `requirement.22.QuoteSpliceEmissionLowering@L84814`, `diagnostics.22.QuoteSpliceEmissionDiagnosticsReference@L84829`, `grammar.22.DeriveTargetsAndContracts@L84846`, `def.22.DeriveParseJudg@L84867`, `requirement.22.DeriveAttributeParsingReference@L84880`, `rule.22.Parse-DeriveTargetDecl@L84893`, `rule.22.Parse-DeriveContractOpt-None@L84909`, `rule.22.Parse-DeriveContractOpt-Yes@L84925`
- `rule.22.Parse-DeriveClauseList-Cons@L84941`, `rule.22.Parse-DeriveClause-Requires@L84957`, `rule.22.Parse-DeriveClause-Emits@L84973`, `rule.22.Parse-DeriveClauseTail-End@L84989`, `rule.22.Parse-DeriveClauseTail-Comma@L85005`, `def.22.DeriveTargetDecl@L85023`, `def.22.DeriveGraphAndOrder@L85038`, `requirement.22.DeriveAttributeTargetKinds@L85062`
- `requirement.22.DeriveTargetNameResolution@L85075`, `requirement.22.DeriveTargetBodyBindings@L85088`, `requirement.22.DeriveTargetBodyRestrictions@L85105`, `requirement.22.DeriveExecutionOrder@L85118`, `requirement.22.DeriveOrderTieBreaker@L85133`, `requirement.22.DeriveRequiresValidation@L85146`, `requirement.22.DeriveEmitsValidation@L85159`, `requirement.22.DeriveRequiresEmitsScope@L85172`
- `requirement.22.DeriveTargetDeclPhase2Lifetime@L85187`, `rule.22.CtExpandItem-DeriveTargetDecl@L85200`, `rule.22.RunDeriveSet-Empty@L85216`, `rule.22.RunDeriveSet-Cons@L85231`, `rule.22.RunDeriveTarget@L85247`, `def.22.BindDeriveTargetInputs@L85263`, `rule.22.CtExpandItem-DeriveAnnotatedDecl@L85276`, `requirement.22.DeriveTargetExecutionTiming@L85292`
- `requirement.22.DeriveTargetFailureSemantics@L85305`, `requirement.22.DeriveTargetsLowering@L85320`, `diagnostics.22.DeriveTargetsDiagnosticsReference@L85335`, `diagnostics.22.CompileTimeDiagnosticsSupplement@L85350`, `requirement.22.UserDiagnosticBuiltinEmission@L85426`

#### `spec.ffi`

Count: 203 total; 203 required; 0 recommended; 0 informative. Ledger line span: L85054-L88478.

- `requirement.23.FFIBoundaryDefinition@L85443`, `def.23.FFIBoundary@L85456`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85473`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85488`, `def.23.FfiSafeTypePredicateAstForm@L85503`, `def.23.FfiSafePredicateMeaning@L85518`, `def.23.FfiSafeJudgements@L85531`, `def.23.FfiPrimitiveTypes@L85544`
- `def.23.FfiLayoutAndPayloadHelpers@L85557`, `def.23.FfiTypeParameterSetHelper@L85573`, `def.23.FfiAliasHelpers@L85586`, `def.23.TypeSubst@L85600`, `def.23.TypeParamsIn@L85637`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85675`, `def.23.FfiSafePredicateClauseHelpers@L85689`, `def.23.ProhibitedFfiType@L85703`
- `def.23.FfiByValueHelpers@L85734`, `rule.23.FfiSafe-Prim@L85749`, `rule.23.FfiSafe-RawPtr@L85765`, `rule.23.FfiSafe-Array@L85781`, `rule.23.FfiSafe-Func@L85797`, `rule.23.FfiSafe-Perm@L85813`, `rule.23.FfiSafe-Alias@L85829`, `rule.23.FfiSafe-Alias-Apply@L85845`
- `rule.23.FfiSafe-Record@L85861`, `rule.23.FfiSafe-Record-Apply@L85877`, `rule.23.FfiSafe-Enum@L85893`, `rule.23.FfiSafe-Enum-Apply@L85909`, `rule.23.FfiSafe-Prohibited-Err@L85925`, `rule.23.FfiSafe-Record-LayoutC-Err@L85941`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85957`, `rule.23.FfiSafe-Record-Field-Err@L85973`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85989`, `rule.23.FfiSafe-Enum-Field-Err@L86005`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L86021`, `rule.23.FfiSafe-Incomplete-Err@L86037`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L86053`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L86069`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L86085`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L86101`
- `requirement.23.FfiSafeProhibitedCategories@L86117`, `requirement.23.FfiSafeRaiiByValueRule@L86144`, `requirement.23.FfiSafeGenericBounds@L86157`, `requirement.23.FfiSafeDynamicSemantics@L86172`, `requirement.23.FfiSafeLowering@L86187`, `diagnostics.23.FfiSafeDiagnostics@L86202`, `grammar.23.ExternProcedureDecl@L86228`, `rule.23.Parse-ExternProcDecl@L86245`
- `ast.23.ExternProcDeclForm@L86263`, `def.23.ExternProcedureDerivedForms@L86280`, `def.23.ExternProcedureMeaning@L86300`, `def.23.ExternAbiStrings@L86313`, `def.23.ExternSignatureRequirements@L86335`, `requirement.23.ExternFfiConstraints@L86358`, `requirement.23.ExternCallSafety@L86375`, `requirement.23.ExternDynamicSemantics@L86392`
- `requirement.23.ExternLowering@L86407`, `diagnostics.23.ExternProcedureDiagnostics@L86422`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86438`, `requirement.23.RawExportedProcedureClassification@L86455`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86470`, `requirement.23.RawExportParsingClassification@L86483`, `ast.23.RawExportProcedureForm@L86498`, `def.23.RawExportedProcedureMeaning@L86515`
- `def.23.ZeroValueHelpers@L86528`, `def.23.ExportSignatureHelpers@L86545`, `rule.23.ExportSig-Ok@L86559`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86577`, `requirement.23.RawExportLibraryImageLifecycle@L86590`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86603`, `requirement.23.RawExportLowering@L86618`, `diagnostics.23.RawExportDiagnostics@L86633`
- `diagnostics.23.RawExportDiagnosticOwnership@L86649`, `requirement.23.HostedExportClassification@L86664`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86679`, `requirement.23.HostedExportParsingClassification@L86692`, `ast.23.HostedExportProcedureForm@L86707`, `def.23.HostedExportProcedureHelpers@L86720`, `requirement.23.HostedRootCapsMeaning@L86747`, `def.23.HostedExportMeaning@L86762`
- `requirement.23.HostedExportForeignVisibleSignature@L86775`, `requirement.23.HostedExportForeignVisiblePassKind@L86788`, `def.23.HostExportSignatureJudgements@L86801`, `rule.23.HostExportSig-Ok@L86814`, `rule.23.HostExport-Library-Err@L86830`, `rule.23.HostExport-MixedMode-Err@L86846`, `rule.23.HostExport-Generic-Err@L86862`, `rule.23.HostExport-Context-Err@L86878`
- `rule.23.HostExport-Context-Raw-Err@L86894`, `rule.23.HostExport-Context-Move-Err@L86910`, `requirement.23.HostedExportSessionHandleValidity@L86928`, `requirement.23.HostedExportCapabilityIsolation@L86941`, `requirement.23.HostedSessionRootCapsGrant@L86954`, `requirement.23.HostedExportBoundaryEntrySequence@L86967`, `requirement.23.HostedExportInvalidHandleBehavior@L86986`, `requirement.23.HostedExportCatchFailureReturn@L87002`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L87017`, `requirement.23.HostedExportThunkAbiDetermination@L87030`, `def.23.HostThunkCarrierHelpers@L87048`, `rule.23.HostThunkParamCarrier-ByRef@L87076`, `rule.23.HostThunkParamCarrier-ByValue-Default@L87092`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87108`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87124`, `rule.23.HostThunkRetCarrier-Default@L87140`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87156`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87172`, `requirement.23.HostedExportThunkShapeUse@L87188`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87201`, `requirement.23.HostedExportNoExtraAbiRewriting@L87214`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87227`, `requirement.23.HostedThunkToSourceCallReconstruction@L87240`, `requirement.23.HostedStateSymbolResolution@L87253`
- `requirement.23.HostedLibraryLifecycleExports@L87266`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87283`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87296`, `requirement.23.HostedSessionHandleNoReissue@L87309`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87322`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87341`, `diagnostics.23.HostedExportDiagnostics@L87356`, `diagnostics.23.HostedExportDiagnosticOwnership@L87375`
- `grammar.23.FfiAttributes@L87392`, `requirement.23.FfiAttributesParsing@L87421`, `ast.23.FfiAttributesAttachedEntries@L87436`, `ast.23.FfiAttributeTargets@L87449`, `requirement.23.MangleAttributeSemantics@L87473`, `def.23.LibraryLinkKinds@L87492`, `requirement.23.LibraryAttributeSemantics@L87512`, `def.23.ResolveLibraryName@L87529`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87556`, `requirement.23.RawDylibResolution@L87569`, `def.23.UnwindModes@L87587`, `requirement.23.UnwindDefaultMode@L87605`, `requirement.23.UnwindAttributeTargetValidity@L87618`, `requirement.23.UnwindCatchAbiRequirement@L87631`, `requirement.23.ExportAttributeSemantics@L87651`, `requirement.23.HostExportAttributeSemantics@L87671`
- `requirement.23.FfiPassByValueAttributeSemantics@L87693`, `requirement.23.FfiAttributeConstraints@L87706`, `requirement.23.FfiAttributesDynamicSemantics@L87732`, `requirement.23.FfiAttributesLowering@L87747`, `diagnostics.23.FfiAttributeDiagnostics@L87762`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87795`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87810`, `ast.23.CapabilityIsolationNoDedicatedAst@L87825`
- `requirement.23.CapabilityIsolationSemantics@L87840`, `def.23.CapabilityIsolationHelpers@L87856`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87873`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87891`, `requirement.23.CapabilityIsolationDynamicSemantics@L87911`, `requirement.23.CapabilityIsolationLowering@L87926`, `diagnostics.23.CapabilityIsolationDiagnostics@L87941`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87956`
- `grammar.23.ForeignContracts@L87973`, `def.23.ForeignContractStart@L87998`, `rule.23.Parse-ForeignContractClauseListOpt-None@L88011`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L88027`, `rule.23.Parse-ForeignContractClauseList-Cons@L88043`, `rule.23.Parse-ForeignContractClauseListTail-End@L88059`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L88075`, `rule.23.Parse-ForeignContractClause-Assumes@L88091`
- `rule.23.Parse-ForeignContractClause-Ensures@L88107`, `def.23.ForeignEnsuresKindAndExpr@L88123`, `rule.23.Parse-EnsuresPredicate-Error@L88142`, `rule.23.Parse-EnsuresPredicate-NullResult@L88158`, `rule.23.Parse-EnsuresPredicate-Plain@L88174`, `ast.23.ForeignContractsForm@L88191`, `ast.23.EnsuresPredicateForms@L88214`, `def.23.ForeignPreconditions@L88236`
- `requirement.23.ForeignPredicateContext@L88249`, `def.23.ForeignPreconditionVerificationModes@L88275`, `requirement.23.ForeignPreconditionVerificationLowering@L88293`, `def.23.ForeignPostconditions@L88308`, `requirement.23.ForeignPostconditionPredicateBindings@L88321`, `def.23.ForeignPostconditionClassification@L88341`, `requirement.23.NullResultWellFormedness@L88374`, `def.23.NullableFfiResult@L88390`
- `rule.23.ForeignEnsures-NullResult-Err@L88406`, `requirement.23.ErrorPredicateWellFormedness@L88425`, `def.23.ForeignPostconditionVerificationModes@L88438`, `requirement.23.ForeignPostconditionStaticVerification@L88456`, `def.23.ForeignContractVerificationSummary@L88471`, `requirement.23.ForeignPreconditionDynamicFailure@L88491`, `requirement.23.ForeignPostconditionDynamicChecks@L88504`, `requirement.23.ForeignContractsLowering@L88519`
- `diagnostics.23.ForeignContractDiagnostics@L88534`, `requirement.23.BoundaryUnwindingSyntax@L88561`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88576`, `ast.23.BoundaryUnwindPolicySource@L88591`, `def.23.UnwindModeAstHelpers@L88604`, `def.23.DetermineUnwindMode@L88626`, `def.23.ParseUnwindArg@L88651`, `rule.23.UnwindMode-Valid@L88673`
- `rule.23.UnwindMode-Invalid-Err@L88691`, `requirement.23.BoundaryUnwindDynamicEffects@L88711`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88730`, `def.23.BoundaryUnwindCodeGenerationEffects@L88745`, `rule.23.CodeGen-UnwindAbort-Import@L88765`, `rule.23.CodeGen-UnwindCatch-Import@L88783`, `rule.23.CodeGen-UnwindAbort-Export@L88801`, `rule.23.CodeGen-UnwindCatch-Export@L88819`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88839`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88852`, `diagnostics.23.FfiDiagnosticsSupplement@L88867`
- `requirement.23.FFIBoundaryDefinition@L85443`, `def.23.FFIBoundary@L85456`, `requirement.23.FfiSafeSyntaxNoAdditionalForm@L85473`, `requirement.23.FfiSafeParsingNoAdditionalRules@L85488`, `def.23.FfiSafeTypePredicateAstForm@L85503`, `def.23.FfiSafePredicateMeaning@L85518`, `def.23.FfiSafeJudgements@L85531`, `def.23.FfiPrimitiveTypes@L85544`
- `def.23.FfiLayoutAndPayloadHelpers@L85557`, `def.23.FfiTypeParameterSetHelper@L85573`, `def.23.FfiAliasHelpers@L85586`, `def.23.TypeSubst@L85600`, `def.23.TypeParamsIn@L85637`, `def.23.FfiFieldAndPayloadTypeParamHelpers@L85675`, `def.23.FfiSafePredicateClauseHelpers@L85689`, `def.23.ProhibitedFfiType@L85703`
- `def.23.FfiByValueHelpers@L85734`, `rule.23.FfiSafe-Prim@L85749`, `rule.23.FfiSafe-RawPtr@L85765`, `rule.23.FfiSafe-Array@L85781`, `rule.23.FfiSafe-Func@L85797`, `rule.23.FfiSafe-Perm@L85813`, `rule.23.FfiSafe-Alias@L85829`, `rule.23.FfiSafe-Alias-Apply@L85845`
- `rule.23.FfiSafe-Record@L85861`, `rule.23.FfiSafe-Record-Apply@L85877`, `rule.23.FfiSafe-Enum@L85893`, `rule.23.FfiSafe-Enum-Apply@L85909`, `rule.23.FfiSafe-Prohibited-Err@L85925`, `rule.23.FfiSafe-Record-LayoutC-Err@L85941`, `rule.23.FfiSafe-Enum-LayoutC-Err@L85957`, `rule.23.FfiSafe-Record-Field-Err@L85973`
- `rule.23.FfiSafe-Record-Field-Apply-Err@L85989`, `rule.23.FfiSafe-Enum-Field-Err@L86005`, `rule.23.FfiSafe-Enum-Field-Apply-Err@L86021`, `rule.23.FfiSafe-Incomplete-Err@L86037`, `rule.23.FfiSafe-Record-Generic-Unbounded-Err@L86053`, `rule.23.FfiSafe-Enum-Generic-Unbounded-Err@L86069`, `rule.23.FfiSafe-Record-Apply-Generic-Unbounded-Err@L86085`, `rule.23.FfiSafe-Enum-Apply-Generic-Unbounded-Err@L86101`
- `requirement.23.FfiSafeProhibitedCategories@L86117`, `requirement.23.FfiSafeRaiiByValueRule@L86144`, `requirement.23.FfiSafeGenericBounds@L86157`, `requirement.23.FfiSafeDynamicSemantics@L86172`, `requirement.23.FfiSafeLowering@L86187`, `diagnostics.23.FfiSafeDiagnostics@L86202`, `grammar.23.ExternProcedureDecl@L86228`, `rule.23.Parse-ExternProcDecl@L86245`
- `ast.23.ExternProcDeclForm@L86263`, `def.23.ExternProcedureDerivedForms@L86280`, `def.23.ExternProcedureMeaning@L86300`, `def.23.ExternAbiStrings@L86313`, `def.23.ExternSignatureRequirements@L86335`, `requirement.23.ExternFfiConstraints@L86358`, `requirement.23.ExternCallSafety@L86375`, `requirement.23.ExternDynamicSemantics@L86392`
- `requirement.23.ExternLowering@L86407`, `diagnostics.23.ExternProcedureDiagnostics@L86422`, `diagnostics.23.ExternProcedureDiagnosticOwnership@L86438`, `requirement.23.RawExportedProcedureClassification@L86455`, `requirement.23.RawExportParsingUsesOrdinaryProcedureParser@L86470`, `requirement.23.RawExportParsingClassification@L86483`, `ast.23.RawExportProcedureForm@L86498`, `def.23.RawExportedProcedureMeaning@L86515`
- `def.23.ZeroValueHelpers@L86528`, `def.23.ExportSignatureHelpers@L86545`, `rule.23.ExportSig-Ok@L86559`, `requirement.23.RawExportOrdinaryBodyAndCatchReturn@L86577`, `requirement.23.RawExportLibraryImageLifecycle@L86590`, `requirement.23.SharedLibraryLinkedCallLifecycle@L86603`, `requirement.23.RawExportLowering@L86618`, `diagnostics.23.RawExportDiagnostics@L86633`
- `diagnostics.23.RawExportDiagnosticOwnership@L86649`, `requirement.23.HostedExportClassification@L86664`, `requirement.23.HostedExportParsingUsesOrdinaryProcedureParser@L86679`, `requirement.23.HostedExportParsingClassification@L86692`, `ast.23.HostedExportProcedureForm@L86707`, `def.23.HostedExportProcedureHelpers@L86720`, `requirement.23.HostedRootCapsMeaning@L86747`, `def.23.HostedExportMeaning@L86762`
- `requirement.23.HostedExportForeignVisibleSignature@L86775`, `requirement.23.HostedExportForeignVisiblePassKind@L86788`, `def.23.HostExportSignatureJudgements@L86801`, `rule.23.HostExportSig-Ok@L86814`, `rule.23.HostExport-Library-Err@L86830`, `rule.23.HostExport-MixedMode-Err@L86846`, `rule.23.HostExport-Generic-Err@L86862`, `rule.23.HostExport-Context-Err@L86878`
- `rule.23.HostExport-Context-Raw-Err@L86894`, `rule.23.HostExport-Context-Move-Err@L86910`, `requirement.23.HostedExportSessionHandleValidity@L86928`, `requirement.23.HostedExportCapabilityIsolation@L86941`, `requirement.23.HostedSessionRootCapsGrant@L86954`, `requirement.23.HostedExportBoundaryEntrySequence@L86967`, `requirement.23.HostedExportInvalidHandleBehavior@L86986`, `requirement.23.HostedExportCatchFailureReturn@L87002`
- `requirement.23.HostedExportLoweringPreservesRawFfiRules@L87017`, `requirement.23.HostedExportThunkAbiDetermination@L87030`, `def.23.HostThunkCarrierHelpers@L87048`, `rule.23.HostThunkParamCarrier-ByRef@L87076`, `rule.23.HostThunkParamCarrier-ByValue-Default@L87092`, `rule.23.HostThunkParamCarrier-Win64-DirectAgg@L87108`, `rule.23.HostThunkParamCarrier-Win64-IndirectAgg@L87124`, `rule.23.HostThunkRetCarrier-Default@L87140`
- `rule.23.HostThunkRetCarrier-Win64-DirectAgg@L87156`, `rule.23.HostThunkRetCarrier-Win64-SRetAgg@L87172`, `requirement.23.HostedExportThunkShapeUse@L87188`, `requirement.23.HostedExportNoWin64AggregateSplitting@L87201`, `requirement.23.HostedExportNoExtraAbiRewriting@L87214`, `requirement.23.HostedThunkModeIndependentForeignClassification@L87227`, `requirement.23.HostedThunkToSourceCallReconstruction@L87240`, `requirement.23.HostedStateSymbolResolution@L87253`
- `requirement.23.HostedLibraryLifecycleExports@L87266`, `requirement.23.HostedLifecycleExportsBackendGenerated@L87283`, `requirement.23.HostedLifecycleExportsPanicAndDestroyFailure@L87296`, `requirement.23.HostedSessionHandleNoReissue@L87309`, `requirement.23.HostedExportThunkForeignVisibleAbi@L87322`, `requirement.23.HostedExportThunkEmissionAndEntrypoint@L87341`, `diagnostics.23.HostedExportDiagnostics@L87356`, `diagnostics.23.HostedExportDiagnosticOwnership@L87375`
- `grammar.23.FfiAttributes@L87392`, `requirement.23.FfiAttributesParsing@L87421`, `ast.23.FfiAttributesAttachedEntries@L87436`, `ast.23.FfiAttributeTargets@L87449`, `requirement.23.MangleAttributeSemantics@L87473`, `def.23.LibraryLinkKinds@L87492`, `requirement.23.LibraryAttributeSemantics@L87512`, `def.23.ResolveLibraryName@L87529`
- `requirement.23.UnsupportedLibraryKindIllFormed@L87556`, `requirement.23.RawDylibResolution@L87569`, `def.23.UnwindModes@L87587`, `requirement.23.UnwindDefaultMode@L87605`, `requirement.23.UnwindAttributeTargetValidity@L87618`, `requirement.23.UnwindCatchAbiRequirement@L87631`, `requirement.23.ExportAttributeSemantics@L87651`, `requirement.23.HostExportAttributeSemantics@L87671`
- `requirement.23.FfiPassByValueAttributeSemantics@L87693`, `requirement.23.FfiAttributeConstraints@L87706`, `requirement.23.FfiAttributesDynamicSemantics@L87732`, `requirement.23.FfiAttributesLowering@L87747`, `diagnostics.23.FfiAttributeDiagnostics@L87762`, `requirement.23.CapabilityIsolationSyntaxNoAdditionalForm@L87795`, `requirement.23.CapabilityIsolationParsingNoAdditionalRules@L87810`, `ast.23.CapabilityIsolationNoDedicatedAst@L87825`
- `requirement.23.CapabilityIsolationSemantics@L87840`, `def.23.CapabilityIsolationHelpers@L87856`, `rule.23.FFI-Arg-RegionLocalRawPtr-Err@L87873`, `rule.23.FFI-Return-RegionLocalRawPtr-Err@L87891`, `requirement.23.CapabilityIsolationDynamicSemantics@L87911`, `requirement.23.CapabilityIsolationLowering@L87926`, `diagnostics.23.CapabilityIsolationDiagnostics@L87941`, `diagnostics.23.CapabilityIsolationDiagnosticOwnership@L87956`
- `grammar.23.ForeignContracts@L87973`, `def.23.ForeignContractStart@L87998`, `rule.23.Parse-ForeignContractClauseListOpt-None@L88011`, `rule.23.Parse-ForeignContractClauseListOpt-Yes@L88027`, `rule.23.Parse-ForeignContractClauseList-Cons@L88043`, `rule.23.Parse-ForeignContractClauseListTail-End@L88059`, `rule.23.Parse-ForeignContractClauseListTail-Cons@L88075`, `rule.23.Parse-ForeignContractClause-Assumes@L88091`
- `rule.23.Parse-ForeignContractClause-Ensures@L88107`, `def.23.ForeignEnsuresKindAndExpr@L88123`, `rule.23.Parse-EnsuresPredicate-Error@L88142`, `rule.23.Parse-EnsuresPredicate-NullResult@L88158`, `rule.23.Parse-EnsuresPredicate-Plain@L88174`, `ast.23.ForeignContractsForm@L88191`, `ast.23.EnsuresPredicateForms@L88214`, `def.23.ForeignPreconditions@L88236`
- `requirement.23.ForeignPredicateContext@L88249`, `def.23.ForeignPreconditionVerificationModes@L88275`, `requirement.23.ForeignPreconditionVerificationLowering@L88293`, `def.23.ForeignPostconditions@L88308`, `requirement.23.ForeignPostconditionPredicateBindings@L88321`, `def.23.ForeignPostconditionClassification@L88341`, `requirement.23.NullResultWellFormedness@L88374`, `def.23.NullableFfiResult@L88390`
- `rule.23.ForeignEnsures-NullResult-Err@L88406`, `requirement.23.ErrorPredicateWellFormedness@L88425`, `def.23.ForeignPostconditionVerificationModes@L88438`, `requirement.23.ForeignPostconditionStaticVerification@L88456`, `def.23.ForeignContractVerificationSummary@L88471`, `requirement.23.ForeignPreconditionDynamicFailure@L88491`, `requirement.23.ForeignPostconditionDynamicChecks@L88504`, `requirement.23.ForeignContractsLowering@L88519`
- `diagnostics.23.ForeignContractDiagnostics@L88534`, `requirement.23.BoundaryUnwindingSyntax@L88561`, `requirement.23.BoundaryUnwindingParsingNoAdditionalRules@L88576`, `ast.23.BoundaryUnwindPolicySource@L88591`, `def.23.UnwindModeAstHelpers@L88604`, `def.23.DetermineUnwindMode@L88626`, `def.23.ParseUnwindArg@L88651`, `rule.23.UnwindMode-Valid@L88673`
- `rule.23.UnwindMode-Invalid-Err@L88691`, `requirement.23.BoundaryUnwindDynamicEffects@L88711`, `requirement.23.GeneralDestructionAndUnwindCleanupReference@L88730`, `def.23.BoundaryUnwindCodeGenerationEffects@L88745`, `rule.23.CodeGen-UnwindAbort-Import@L88765`, `rule.23.CodeGen-UnwindCatch-Import@L88783`, `rule.23.CodeGen-UnwindAbort-Export@L88801`, `rule.23.CodeGen-UnwindCatch-Export@L88819`
- `diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics@L88839`, `diagnostics.23.BoundaryUnwindingDiagnosticOwnership@L88852`, `diagnostics.23.FfiDiagnosticsSupplement@L88867`

#### `spec.lowering`

Count: 158 total; 158 required; 0 recommended; 0 informative. Ledger line span: L88499-L91244.

- `requirement.24.SharedLoweringScope@L88888`, `def.24.CodegenModelAndTargets@L88903`, `def.24.CodegenJudgements@L88922`, `def.24.IRDefined@L88935`, `def.24.CodegenCorrectnessPredicates@L88948`, `def.24.CodegenCorrectAndUndefined@L88965`, `def.24.IRFormsAndEmissionJudgements@L88982`, `def.24.PanicOutCodegenParams@L89000`
- `def.24.MethodAndTransitionParams@L89014`, `def.24.SeqIR@L89032`, `def.24.EvalOrderJudgements@L89047`, `def.24.ChildExpressionListHelpers@L89060`, `def.24.ChildrenLTRExpressions@L89106`, `def.24.LowerExprJudgementsAndRetType@L89157`, `rule.24.Lower-Expr-Correctness@L89172`, `def.24.LowerExprTotal@L89188`
- `def.24.ExecIRJudgements@L89202`, `rule.24.ExecIR-ReadVar@L89215`, `rule.24.ExecIR-ReadPath@L89231`, `rule.24.ExecIR-StoreVar@L89247`, `rule.24.ExecIR-StoreVarNoDrop@L89263`, `rule.24.ExecIR-BindVar@L89279`, `rule.24.ExecIR-ReadPtr@L89295`, `rule.24.ExecIR-WritePtr@L89311`
- `def.24.AllocTarget@L89327`, `rule.24.ExecIR-Alloc@L89341`, `rule.24.MoveState-Root@L89357`, `rule.24.MoveState-Field@L89373`, `rule.24.ExecIR-MoveState@L89389`, `def.24.ExecIRControlResults@L89405`, `rule.24.ExecIR-Defer@L89421`, `def.24.ExecIRBlockHelpers@L89437`
- `rule.24.ExecIR-If-True@L89452`, `rule.24.ExecIR-If-False@L89468`, `rule.24.ExecIR-Block@L89484`, `rule.24.ExecIR-IfCase@L89500`, `rule.24.ExecIR-Loop-Infinite-Step@L89516`, `rule.24.ExecIR-Loop-Infinite-Continue@L89532`, `rule.24.ExecIR-Loop-Infinite-Break@L89548`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89564`
- `rule.24.ExecIR-Loop-Cond-False@L89580`, `rule.24.ExecIR-Loop-Cond-True-Step@L89596`, `rule.24.ExecIR-Loop-Cond-Continue@L89612`, `rule.24.ExecIR-Loop-Cond-Break@L89628`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89644`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89660`, `def.24.LoopIterIRJudgement@L89676`, `rule.24.ExecIR-Loop-Iter@L89689`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89705`, `rule.24.LoopIterIR-Done@L89721`, `rule.24.LoopIterIR-Step-Val@L89737`, `rule.24.LoopIterIR-Step-Continue@L89753`, `rule.24.LoopIterIR-Step-Break@L89769`, `rule.24.LoopIterIR-Step-Ctrl@L89785`, `rule.24.ExecIR-Region@L89801`, `rule.24.ExecIR-Frame-Implicit@L89817`
- `rule.24.ExecIR-Frame-Explicit@L89833`, `rule.24.LowerList-Empty@L89849`, `rule.24.LowerList-Cons@L89864`, `rule.24.LowerFieldInits-Empty@L89880`, `rule.24.LowerFieldInits-Cons@L89895`, `rule.24.LowerOpt-None@L89911`, `rule.24.LowerOpt-Some@L89926`, `def.24.RefSyms@L89942`
- `def.24.ExpandIR@L90004`, `def.24.UniqueEmits@L90017`, `def.24.ModuleItems@L90039`, `rule.24.CG-Project@L90052`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L90068`, `rule.24.CG-Module@L90081`, `rule.24.CG-Expr@L90097`, `rule.24.CG-Stmt@L90113`
- `rule.24.CG-Block@L90129`, `rule.24.CG-Place@L90145`, `rule.24.LowerIR-Module@L90161`, `rule.24.LowerIR-Err@L90177`, `rule.24.EmitLLVM-Ok@L90193`, `def.24.LLVMText21Acceptance@L90209`, `rule.24.EmitLLVM-Err@L90223`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90239`
- `rule.24.EmitObj-Ok@L90253`, `def.24.LLVMEmitObj21@L90269`, `rule.24.EmitObj-Err@L90282`, `def.24.PointerPrimitiveSizeAndAlignment@L90302`, `def.24.LayoutJudgements@L90357`, `rule.24.Size-Prim@L90370`, `rule.24.Align-Prim@L90386`, `rule.24.Layout-Prim@L90402`
- `def.24.ConstantEncodingHelpers@L90418`, `rule.24.Encode-Bool@L90435`, `rule.24.Encode-Char@L90451`, `rule.24.Encode-Int@L90467`, `rule.24.Encode-Float@L90483`, `rule.24.Encode-Unit@L90499`, `rule.24.Encode-Never@L90515`, `rule.24.Encode-RawPtr-Null@L90531`
- `def.24.ValidValueJudgement@L90547`, `rule.24.Valid-Bool@L90560`, `rule.24.Valid-Char@L90574`, `rule.24.Valid-Scalar@L90588`, `rule.24.Valid-Unit@L90603`, `rule.24.Valid-Never@L90617`, `def.24.ValidValueFallback@L90631`, `rule.24.Layout-Perm@L90647`
- `rule.24.Size-Perm@L90663`, `rule.24.Align-Perm@L90679`, `def.24.ValueBitsPerm@L90695`, `rule.24.Size-Ptr@L90708`, `rule.24.Align-Ptr@L90724`, `rule.24.Layout-Ptr@L90740`, `rule.24.Size-RawPtr@L90756`, `rule.24.Align-RawPtr@L90772`
- `rule.24.Layout-RawPtr@L90788`, `rule.24.Size-Func@L90804`, `rule.24.Align-Func@L90820`, `rule.24.Layout-Func@L90836`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90854`, `def.24.ExternAbiSetAndConventionMapping@L90934`, `def.24.ConventionLayout@L90956`, `def.24.AssignParamRegs@L91006`
- `def.24.StackFrameForm@L91032`, `rule.24.StackFrame-Layout@L91052`, `rule.24.Conv-Compatible@L91069`, `rule.24.Conv-FFI-Required@L91085`, `def.24.ABITypeAndABITyJudgement@L91103`, `rule.24.ABI-Prim@L91117`, `rule.24.ABI-Perm@L91133`, `rule.24.ABI-Ptr@L91149`
- `rule.24.ABI-RawPtr@L91165`, `rule.24.ABI-Func@L91181`, `rule.24.ABI-Alias@L91197`, `rule.24.ABI-Record@L91213`, `rule.24.ABI-Tuple@L91229`, `rule.24.ABI-Array@L91245`, `rule.24.ABI-Slice@L91261`, `rule.24.ABI-Range@L91277`
- `rule.24.ABI-RangeInclusive@L91293`, `rule.24.ABI-RangeFrom@L91309`, `rule.24.ABI-RangeTo@L91325`, `rule.24.ABI-RangeToInclusive@L91341`, `rule.24.ABI-RangeFull@L91357`, `rule.24.ABI-Enum@L91373`, `rule.24.ABI-Union@L91389`, `rule.24.ABI-Modal@L91405`
- `rule.24.ABI-Dynamic@L91421`, `rule.24.ABI-StringBytes@L91437`, `def.24.ABIParameterReturnPassingHelpers@L91455`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91476`, `rule.24.ABI-Param-ByRef-Alias@L91489`, `rule.24.ABI-Param-ByValue-Move@L91505`, `rule.24.ABI-Param-ByRef-Move@L91521`, `rule.24.ABI-Ret-ByValue@L91537`
- `rule.24.ABI-Ret-ByRef@L91553`, `rule.24.ABI-Call@L91569`, `rule.24.ABI-ForeignParam-ByValue@L91585`, `rule.24.ABI-ForeignParam-ByRef@L91601`, `rule.24.ABI-ForeignCall@L91617`, `def.24.PanicRecordAndPanicOut@L91633`
- `requirement.24.SharedLoweringScope@L88888`, `def.24.CodegenModelAndTargets@L88903`, `def.24.CodegenJudgements@L88922`, `def.24.IRDefined@L88935`, `def.24.CodegenCorrectnessPredicates@L88948`, `def.24.CodegenCorrectAndUndefined@L88965`, `def.24.IRFormsAndEmissionJudgements@L88982`, `def.24.PanicOutCodegenParams@L89000`
- `def.24.MethodAndTransitionParams@L89014`, `def.24.SeqIR@L89032`, `def.24.EvalOrderJudgements@L89047`, `def.24.ChildExpressionListHelpers@L89060`, `def.24.ChildrenLTRExpressions@L89106`, `def.24.LowerExprJudgementsAndRetType@L89157`, `rule.24.Lower-Expr-Correctness@L89172`, `def.24.LowerExprTotal@L89188`
- `def.24.ExecIRJudgements@L89202`, `rule.24.ExecIR-ReadVar@L89215`, `rule.24.ExecIR-ReadPath@L89231`, `rule.24.ExecIR-StoreVar@L89247`, `rule.24.ExecIR-StoreVarNoDrop@L89263`, `rule.24.ExecIR-BindVar@L89279`, `rule.24.ExecIR-ReadPtr@L89295`, `rule.24.ExecIR-WritePtr@L89311`
- `def.24.AllocTarget@L89327`, `rule.24.ExecIR-Alloc@L89341`, `rule.24.MoveState-Root@L89357`, `rule.24.MoveState-Field@L89373`, `rule.24.ExecIR-MoveState@L89389`, `def.24.ExecIRControlResults@L89405`, `rule.24.ExecIR-Defer@L89421`, `def.24.ExecIRBlockHelpers@L89437`
- `rule.24.ExecIR-If-True@L89452`, `rule.24.ExecIR-If-False@L89468`, `rule.24.ExecIR-Block@L89484`, `rule.24.ExecIR-IfCase@L89500`, `rule.24.ExecIR-Loop-Infinite-Step@L89516`, `rule.24.ExecIR-Loop-Infinite-Continue@L89532`, `rule.24.ExecIR-Loop-Infinite-Break@L89548`, `rule.24.ExecIR-Loop-Infinite-Ctrl@L89564`
- `rule.24.ExecIR-Loop-Cond-False@L89580`, `rule.24.ExecIR-Loop-Cond-True-Step@L89596`, `rule.24.ExecIR-Loop-Cond-Continue@L89612`, `rule.24.ExecIR-Loop-Cond-Break@L89628`, `rule.24.ExecIR-Loop-Cond-Ctrl@L89644`, `rule.24.ExecIR-Loop-Cond-Body-Ctrl@L89660`, `def.24.LoopIterIRJudgement@L89676`, `rule.24.ExecIR-Loop-Iter@L89689`
- `rule.24.ExecIR-Loop-Iter-Ctrl@L89705`, `rule.24.LoopIterIR-Done@L89721`, `rule.24.LoopIterIR-Step-Val@L89737`, `rule.24.LoopIterIR-Step-Continue@L89753`, `rule.24.LoopIterIR-Step-Break@L89769`, `rule.24.LoopIterIR-Step-Ctrl@L89785`, `rule.24.ExecIR-Region@L89801`, `rule.24.ExecIR-Frame-Implicit@L89817`
- `rule.24.ExecIR-Frame-Explicit@L89833`, `rule.24.LowerList-Empty@L89849`, `rule.24.LowerList-Cons@L89864`, `rule.24.LowerFieldInits-Empty@L89880`, `rule.24.LowerFieldInits-Cons@L89895`, `rule.24.LowerOpt-None@L89911`, `rule.24.LowerOpt-Some@L89926`, `def.24.RefSyms@L89942`
- `def.24.ExpandIR@L90004`, `def.24.UniqueEmits@L90017`, `def.24.ModuleItems@L90039`, `rule.24.CG-Project@L90052`, `requirement.24.NoAdditionalFeatureLocalCodegenItemRules@L90068`, `rule.24.CG-Module@L90081`, `rule.24.CG-Expr@L90097`, `rule.24.CG-Stmt@L90113`
- `rule.24.CG-Block@L90129`, `rule.24.CG-Place@L90145`, `rule.24.LowerIR-Module@L90161`, `rule.24.LowerIR-Err@L90177`, `rule.24.EmitLLVM-Ok@L90193`, `def.24.LLVMText21Acceptance@L90209`, `rule.24.EmitLLVM-Err@L90223`, `requirement.24.LLVMToolAcceptanceAndResolveOwnership@L90239`
- `rule.24.EmitObj-Ok@L90253`, `def.24.LLVMEmitObj21@L90269`, `rule.24.EmitObj-Err@L90282`, `def.24.PointerPrimitiveSizeAndAlignment@L90302`, `def.24.LayoutJudgements@L90357`, `rule.24.Size-Prim@L90370`, `rule.24.Align-Prim@L90386`, `rule.24.Layout-Prim@L90402`
- `def.24.ConstantEncodingHelpers@L90418`, `rule.24.Encode-Bool@L90435`, `rule.24.Encode-Char@L90451`, `rule.24.Encode-Int@L90467`, `rule.24.Encode-Float@L90483`, `rule.24.Encode-Unit@L90499`, `rule.24.Encode-Never@L90515`, `rule.24.Encode-RawPtr-Null@L90531`
- `def.24.ValidValueJudgement@L90547`, `rule.24.Valid-Bool@L90560`, `rule.24.Valid-Char@L90574`, `rule.24.Valid-Scalar@L90588`, `rule.24.Valid-Unit@L90603`, `rule.24.Valid-Never@L90617`, `def.24.ValidValueFallback@L90631`, `rule.24.Layout-Perm@L90647`
- `rule.24.Size-Perm@L90663`, `rule.24.Align-Perm@L90679`, `def.24.ValueBitsPerm@L90695`, `rule.24.Size-Ptr@L90708`, `rule.24.Align-Ptr@L90724`, `rule.24.Layout-Ptr@L90740`, `rule.24.Size-RawPtr@L90756`, `rule.24.Align-RawPtr@L90772`
- `rule.24.Layout-RawPtr@L90788`, `rule.24.Size-Func@L90804`, `rule.24.Align-Func@L90820`, `rule.24.Layout-Func@L90836`, `def.24.DefaultCallingConventionAndTargetArtifacts@L90854`, `def.24.ExternAbiSetAndConventionMapping@L90934`, `def.24.ConventionLayout@L90956`, `def.24.AssignParamRegs@L91006`
- `def.24.StackFrameForm@L91032`, `rule.24.StackFrame-Layout@L91052`, `rule.24.Conv-Compatible@L91069`, `rule.24.Conv-FFI-Required@L91085`, `def.24.ABITypeAndABITyJudgement@L91103`, `rule.24.ABI-Prim@L91117`, `rule.24.ABI-Perm@L91133`, `rule.24.ABI-Ptr@L91149`
- `rule.24.ABI-RawPtr@L91165`, `rule.24.ABI-Func@L91181`, `rule.24.ABI-Alias@L91197`, `rule.24.ABI-Record@L91213`, `rule.24.ABI-Tuple@L91229`, `rule.24.ABI-Array@L91245`, `rule.24.ABI-Slice@L91261`, `rule.24.ABI-Range@L91277`
- `rule.24.ABI-RangeInclusive@L91293`, `rule.24.ABI-RangeFrom@L91309`, `rule.24.ABI-RangeTo@L91325`, `rule.24.ABI-RangeToInclusive@L91341`, `rule.24.ABI-RangeFull@L91357`, `rule.24.ABI-Enum@L91373`, `rule.24.ABI-Union@L91389`, `rule.24.ABI-Modal@L91405`
- `rule.24.ABI-Dynamic@L91421`, `rule.24.ABI-StringBytes@L91437`, `def.24.ABIParameterReturnPassingHelpers@L91455`, `requirement.24.ForeignVisibleABIUsesForeignJudgements@L91476`, `rule.24.ABI-Param-ByRef-Alias@L91489`, `rule.24.ABI-Param-ByValue-Move@L91505`, `rule.24.ABI-Param-ByRef-Move@L91521`, `rule.24.ABI-Ret-ByValue@L91537`
- `rule.24.ABI-Ret-ByRef@L91553`, `rule.24.ABI-Call@L91569`, `rule.24.ABI-ForeignParam-ByValue@L91585`, `rule.24.ABI-ForeignParam-ByRef@L91601`, `rule.24.ABI-ForeignCall@L91617`, `def.24.PanicRecordAndPanicOut@L91633`

#### `spec.symbols`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L91273-L92088.

- `def.24.MangleJudgementAndConstructors@L91662`, `def.24.PathSymbolHelpers@L91678`, `def.24.ItemPath@L91695`, `def.24.PathOfTypeAndClassPath@L91717`, `def.24.LiteralSymbolHashing@L91738`, `def.24.ScopedRawAndHostBodySymbols@L91757`, `def.24.AttributeSymbolHelpers@L91772`, `def.24.ExternAbiSymbolHelpers@L91792`
- `def.24.LinkName@L91811`, `def.24.HostThunkLinkNameAndItemName@L91829`, `rule.24.Mangle-HostExport-Proc@L91846`, `rule.24.Mangle-Proc@L91862`, `rule.24.Mangle-ExternProc@L91878`, `rule.24.Mangle-Main@L91894`, `rule.24.Mangle-Record-Method@L91910`, `rule.24.Mangle-Class-Method@L91926`
- `rule.24.Mangle-State-Method@L91942`, `rule.24.Mangle-Transition@L91958`, `rule.24.Mangle-Static@L91974`, `rule.24.Mangle-StaticBinding@L91990`, `rule.24.Mangle-VTable@L92006`, `rule.24.Mangle-Literal@L92022`, `rule.24.Mangle-DefaultImpl@L92038`, `req.24.ClosureIndexUniqueness@L92054`
- `def.24.EnclosingSym@L92067`, `rule.24.Mangle-Closure@L92080`, `rule.24.Mangle-ClosureEnv@L92096`, `def.24.ClosureCodeSym@L92112`, `def.24.LinkageDefinitions@L92127`, `rule.24.Linkage-UserItem@L92141`, `rule.24.Linkage-ExternProc@L92157`, `rule.24.Linkage-UserItem-Internal@L92173`
- `rule.24.Linkage-StaticBinding@L92189`, `rule.24.Linkage-StaticBinding-Internal@L92205`, `rule.24.Linkage-ClassMethod@L92221`, `rule.24.Linkage-ClassMethod-Internal@L92237`, `rule.24.Linkage-StateMethod@L92253`, `rule.24.Linkage-StateMethod-Internal@L92269`, `rule.24.Linkage-Transition@L92285`, `rule.24.Linkage-Transition-Internal@L92301`
- `rule.24.Linkage-InitFn@L92317`, `rule.24.Linkage-DeinitFn@L92333`, `rule.24.Linkage-VTable@L92349`, `rule.24.Linkage-LiteralData@L92365`, `rule.24.Linkage-DropGlue@L92381`, `rule.24.Linkage-DefaultImpl@L92397`, `rule.24.Linkage-DefaultImpl-Internal@L92413`, `rule.24.Linkage-PanicSym@L92429`
- `rule.24.Linkage-BuiltinModalSym@L92445`, `rule.24.Linkage-BuiltinSym@L92461`, `rule.24.Linkage-EntrySym@L92477`
- `def.24.MangleJudgementAndConstructors@L91662`, `def.24.PathSymbolHelpers@L91678`, `def.24.ItemPath@L91695`, `def.24.PathOfTypeAndClassPath@L91717`, `def.24.LiteralSymbolHashing@L91738`, `def.24.ScopedRawAndHostBodySymbols@L91757`, `def.24.AttributeSymbolHelpers@L91772`, `def.24.ExternAbiSymbolHelpers@L91792`
- `def.24.LinkName@L91811`, `def.24.HostThunkLinkNameAndItemName@L91829`, `rule.24.Mangle-HostExport-Proc@L91846`, `rule.24.Mangle-Proc@L91862`, `rule.24.Mangle-ExternProc@L91878`, `rule.24.Mangle-Main@L91894`, `rule.24.Mangle-Record-Method@L91910`, `rule.24.Mangle-Class-Method@L91926`
- `rule.24.Mangle-State-Method@L91942`, `rule.24.Mangle-Transition@L91958`, `rule.24.Mangle-Static@L91974`, `rule.24.Mangle-StaticBinding@L91990`, `rule.24.Mangle-VTable@L92006`, `rule.24.Mangle-Literal@L92022`, `rule.24.Mangle-DefaultImpl@L92038`, `req.24.ClosureIndexUniqueness@L92054`
- `def.24.EnclosingSym@L92067`, `rule.24.Mangle-Closure@L92080`, `rule.24.Mangle-ClosureEnv@L92096`, `def.24.ClosureCodeSym@L92112`, `def.24.LinkageDefinitions@L92127`, `rule.24.Linkage-UserItem@L92141`, `rule.24.Linkage-ExternProc@L92157`, `rule.24.Linkage-UserItem-Internal@L92173`
- `rule.24.Linkage-StaticBinding@L92189`, `rule.24.Linkage-StaticBinding-Internal@L92205`, `rule.24.Linkage-ClassMethod@L92221`, `rule.24.Linkage-ClassMethod-Internal@L92237`, `rule.24.Linkage-StateMethod@L92253`, `rule.24.Linkage-StateMethod-Internal@L92269`, `rule.24.Linkage-Transition@L92285`, `rule.24.Linkage-Transition-Internal@L92301`
- `rule.24.Linkage-InitFn@L92317`, `rule.24.Linkage-DeinitFn@L92333`, `rule.24.Linkage-VTable@L92349`, `rule.24.Linkage-LiteralData@L92365`, `rule.24.Linkage-DropGlue@L92381`, `rule.24.Linkage-DefaultImpl@L92397`, `rule.24.Linkage-DefaultImpl-Internal@L92413`, `rule.24.Linkage-PanicSym@L92429`
- `rule.24.Linkage-BuiltinModalSym@L92445`, `rule.24.Linkage-BuiltinSym@L92461`, `rule.24.Linkage-EntrySym@L92477`

#### `spec.initialization`

Count: 102 total; 102 required; 0 recommended; 0 informative. Ledger line span: L92108-L93623.

- `def.24.GlobalsJudg@L92497`, `def.24.ConstInitJudg@L92510`, `def.24.ConstInitLiteral@L92523`, `def.24.StaticName@L92536`, `def.24.StaticBindTypes@L92551`, `def.24.StaticBindList@L92564`, `def.24.StaticBinding@L92577`, `def.24.StaticSym@L92590`
- `rule.24.Emit-Static-Const@L92605`, `rule.24.Emit-Static-Init@L92621`, `rule.24.Emit-Static-Multi@L92637`, `def.24.InitSym@L92653`, `rule.24.InitFn@L92666`, `def.24.DeinitSym@L92682`, `rule.24.DeinitFn@L92695`, `def.24.StaticItems@L92711`
- `def.24.StaticItemOf@L92724`, `def.24.StaticSymPath@L92737`, `def.24.StaticAddr@L92750`, `req.24.HostedStaticAddrSessionInterpretation@L92763`, `def.24.AddrOfSym@L92776`, `def.24.StaticType@L92789`, `def.24.StaticBindInfo@L92802`, `def.24.SeqIRList@L92815`
- `def.24.StaticStoreIR@L92829`, `rule.24.Lower-StaticInit-Item@L92843`, `rule.24.Lower-StaticInitItems-Empty@L92859`, `rule.24.Lower-StaticInitItems-Cons@L92874`, `rule.24.Lower-StaticInit@L92890`, `rule.24.InitCallIR@L92906`, `def.24.Rev@L92922`, `rule.24.Lower-StaticDeinitNames-Empty@L92936`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92951`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92967`, `rule.24.Lower-StaticDeinit-Item@L92983`, `rule.24.Lower-StaticDeinitItems-Empty@L92999`, `rule.24.Lower-StaticDeinitItems-Cons@L93014`, `rule.24.Lower-StaticDeinit@L93030`, `rule.24.DeinitCallIR@L93046`, `def.24.HostedStateAddressDefinitions@L93062`
- `def.24.LibraryStateSymbolDefinitions@L93077`, `def.24.HostedStateJudg@L93093`, `req.24.SessionStateInitDefinesHostedCells@L93106`, `req.24.SessionStateDestroyRemovesHostedCells@L93119`, `req.24.HostedLibraryStateAddressInterpretation@L93132`, `def.24.InitializationGraphOrdering@L93149`, `rule.24.Topo-Ok@L93168`, `rule.24.Topo-Cycle@L93184`
- `def.24.ProjectInitializationItems@L93200`, `def.24.InitializationPlanDefinitions@L93216`, `def.24.EvalFromEvalSigma@L93238`, `rule.24.EmitInitPlan@L93252`, `rule.24.EmitInitPlan-Err@L93268`, `rule.24.EmitDeinitPlan@L93284`, `rule.24.EmitDeinitPlan-Err@L93300`, `def.24.InitStateMachineDefinitions@L93316`
- `rule.24.Init-Start@L93331`, `rule.24.Init-Step@L93346`, `rule.24.Init-Next-Module@L93362`, `rule.24.Init-Panic@L93378`, `rule.24.Init-Done@L93394`, `rule.24.Init-Ok@L93410`, `rule.24.Init-Fail@L93426`, `rule.24.Deinit-Ok@L93442`
- `rule.24.Deinit-Panic@L93458`, `def.24.EntryJudg@L93476`, `rule.24.EntrySym-Decl@L93489`, `rule.24.ContextInitSym-Decl@L93504`, `def.24.PanicRecordInit@L93554`, `def.24.EntryStubSpec@L93567`, `rule.24.EntryStub-Decl@L93585`, `rule.24.EntrySym-Err@L93601`
- `rule.24.EntryStub-Err@L93617`, `def.24.LibraryImageJudg@L93635`, `def.24.LibraryImageStateDefinitions@L93648`, `req.24.DistinctLibraryImageState@L93668`, `req.24.LibraryImageLivenessTransitions@L93681`, `req.24.LibraryImageInitDefinesSharedCells@L93694`, `req.24.LibraryImageDestroyRemovesSharedCells@L93707`, `req.24.SharedLibraryImageStateInterpretation@L93720`
- `req.24.PartialInitPanicCleanupPrefix@L93733`, `req.24.RawExportImageLifecycle@L93746`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93759`, `req.24.SharedLibraryLoaderEntrypoint@L93772`, `rule.24.LibraryImageInitSigma@L93785`, `rule.24.RawLibraryCallSigma-Ok@L93801`, `rule.24.LibraryImageDestroySigma@L93817`, `def.24.HostedSessionJudg@L93833`
- `def.24.HostedSessionStateDefinitions@L93846`, `req.24.DistinctHostedState@L93868`, `req.24.HostedSessionLifecycleState@L93881`, `req.24.HostedSessionNoConcurrentReentry@L93894`, `rule.24.HostSessionInitSigma@L93907`, `rule.24.HostedCallSigma-Ok@L93923`, `rule.24.HostSessionDestroySigma@L93939`, `def.24.InterpJudg@L93957`
- `def.24.ContextValue@L93970`, `rule.24.ContextInitSigma@L93983`, `rule.24.Interpret-Project-Ok@L93999`, `rule.24.Interpret-Project-Init-Panic@L94015`, `rule.24.Interpret-Project-Main-Ctrl@L94031`, `rule.24.Interpret-Project-Deinit-Panic@L94047`
- `def.24.GlobalsJudg@L92497`, `def.24.ConstInitJudg@L92510`, `def.24.ConstInitLiteral@L92523`, `def.24.StaticName@L92536`, `def.24.StaticBindTypes@L92551`, `def.24.StaticBindList@L92564`, `def.24.StaticBinding@L92577`, `def.24.StaticSym@L92590`
- `rule.24.Emit-Static-Const@L92605`, `rule.24.Emit-Static-Init@L92621`, `rule.24.Emit-Static-Multi@L92637`, `def.24.InitSym@L92653`, `rule.24.InitFn@L92666`, `def.24.DeinitSym@L92682`, `rule.24.DeinitFn@L92695`, `def.24.StaticItems@L92711`
- `def.24.StaticItemOf@L92724`, `def.24.StaticSymPath@L92737`, `def.24.StaticAddr@L92750`, `req.24.HostedStaticAddrSessionInterpretation@L92763`, `def.24.AddrOfSym@L92776`, `def.24.StaticType@L92789`, `def.24.StaticBindInfo@L92802`, `def.24.SeqIRList@L92815`
- `def.24.StaticStoreIR@L92829`, `rule.24.Lower-StaticInit-Item@L92843`, `rule.24.Lower-StaticInitItems-Empty@L92859`, `rule.24.Lower-StaticInitItems-Cons@L92874`, `rule.24.Lower-StaticInit@L92890`, `rule.24.InitCallIR@L92906`, `def.24.Rev@L92922`, `rule.24.Lower-StaticDeinitNames-Empty@L92936`
- `rule.24.Lower-StaticDeinitNames-Cons-Resp@L92951`, `rule.24.Lower-StaticDeinitNames-Cons-NoResp@L92967`, `rule.24.Lower-StaticDeinit-Item@L92983`, `rule.24.Lower-StaticDeinitItems-Empty@L92999`, `rule.24.Lower-StaticDeinitItems-Cons@L93014`, `rule.24.Lower-StaticDeinit@L93030`, `rule.24.DeinitCallIR@L93046`, `def.24.HostedStateAddressDefinitions@L93062`
- `def.24.LibraryStateSymbolDefinitions@L93077`, `def.24.HostedStateJudg@L93093`, `req.24.SessionStateInitDefinesHostedCells@L93106`, `req.24.SessionStateDestroyRemovesHostedCells@L93119`, `req.24.HostedLibraryStateAddressInterpretation@L93132`, `def.24.InitializationGraphOrdering@L93149`, `rule.24.Topo-Ok@L93168`, `rule.24.Topo-Cycle@L93184`
- `def.24.ProjectInitializationItems@L93200`, `def.24.InitializationPlanDefinitions@L93216`, `def.24.EvalFromEvalSigma@L93238`, `rule.24.EmitInitPlan@L93252`, `rule.24.EmitInitPlan-Err@L93268`, `rule.24.EmitDeinitPlan@L93284`, `rule.24.EmitDeinitPlan-Err@L93300`, `def.24.InitStateMachineDefinitions@L93316`
- `rule.24.Init-Start@L93331`, `rule.24.Init-Step@L93346`, `rule.24.Init-Next-Module@L93362`, `rule.24.Init-Panic@L93378`, `rule.24.Init-Done@L93394`, `rule.24.Init-Ok@L93410`, `rule.24.Init-Fail@L93426`, `rule.24.Deinit-Ok@L93442`
- `rule.24.Deinit-Panic@L93458`, `def.24.EntryJudg@L93476`, `rule.24.EntrySym-Decl@L93489`, `rule.24.ContextInitSym-Decl@L93504`, `def.24.PanicRecordInit@L93554`, `def.24.EntryStubSpec@L93567`, `rule.24.EntryStub-Decl@L93585`, `rule.24.EntrySym-Err@L93601`
- `rule.24.EntryStub-Err@L93617`, `def.24.LibraryImageJudg@L93635`, `def.24.LibraryImageStateDefinitions@L93648`, `req.24.DistinctLibraryImageState@L93668`, `req.24.LibraryImageLivenessTransitions@L93681`, `req.24.LibraryImageInitDefinesSharedCells@L93694`, `req.24.LibraryImageDestroyRemovesSharedCells@L93707`, `req.24.SharedLibraryImageStateInterpretation@L93720`
- `req.24.PartialInitPanicCleanupPrefix@L93733`, `req.24.RawExportImageLifecycle@L93746`, `req.24.SharedLibraryLinkedCallImageLifecycle@L93759`, `req.24.SharedLibraryLoaderEntrypoint@L93772`, `rule.24.LibraryImageInitSigma@L93785`, `rule.24.RawLibraryCallSigma-Ok@L93801`, `rule.24.LibraryImageDestroySigma@L93817`, `def.24.HostedSessionJudg@L93833`
- `def.24.HostedSessionStateDefinitions@L93846`, `req.24.DistinctHostedState@L93868`, `req.24.HostedSessionLifecycleState@L93881`, `req.24.HostedSessionNoConcurrentReentry@L93894`, `rule.24.HostSessionInitSigma@L93907`, `rule.24.HostedCallSigma-Ok@L93923`, `rule.24.HostSessionDestroySigma@L93939`, `def.24.InterpJudg@L93957`
- `def.24.ContextValue@L93970`, `rule.24.ContextInitSigma@L93983`, `rule.24.Interpret-Project-Ok@L93999`, `rule.24.Interpret-Project-Init-Panic@L94015`, `rule.24.Interpret-Project-Main-Ctrl@L94031`, `rule.24.Interpret-Project-Deinit-Panic@L94047`

#### `spec.cleanup`

Count: 56 total; 56 required; 0 recommended; 0 informative. Ledger line span: L93645-L94497.

- `def.24.CleanupJudg@L94069`, `rule.24.CleanupPlan@L94082`, `def.24.EmitDropSpec@L94098`, `def.24.PanicOutAddr@L94114`, `def.24.PanicRecordOf@L94127`, `def.24.WritePanicRecord@L94140`, `def.24.InitPanicHandle@L94153`, `req.24.InitPanicHandleResponsiblePrefix@L94166`
- `rule.24.PanicSym@L94179`, `def.24.PanicReasonCodes@L94194`, `def.24.PanicSites@L94219`, `def.24.ClearPanic@L94243`, `def.24.PanicCheck@L94256`, `def.24.LowerPanic@L94269`, `def.24.ResponsibleBinding@L94284`, `grammar.24.CleanupItem@L94297`
- `def.24.DropJudgmentDefinitions@L94310`, `def.24.RecordType@L94327`, `def.24.DropCall@L94340`, `def.24.ReleaseValue@L94357`, `def.24.DropChildren@L94371`, `def.24.DropList@L94391`, `rule.24.DropAction-Moved@L94405`, `rule.24.DropAction-Partial@L94421`
- `rule.24.DropAction-Valid@L94437`, `rule.24.DropStaticAction@L94453`, `def.24.NonRecordFOk@L94469`, `rule.24.DropValueOut-DropPanic@L94482`, `rule.24.DropValueOut-ChildPanic@L94498`, `rule.24.DropValueOut-Ok@L94514`, `def.24.CleanupStateDefinitions@L94532`, `rule.24.Cleanup-Start@L94546`
- `rule.24.Cleanup-Step-Drop-Ok@L94561`, `rule.24.Cleanup-Step-Drop-Panic@L94577`, `rule.24.Cleanup-Step-Drop-Abort@L94593`, `rule.24.Cleanup-Step-DropStatic-Ok@L94609`, `rule.24.Cleanup-Step-DropStatic-Panic@L94625`, `rule.24.Cleanup-Step-DropStatic-Abort@L94641`, `rule.24.Cleanup-Step-Defer-Ok@L94657`, `rule.24.Cleanup-Step-Defer-Panic@L94673`
- `rule.24.Cleanup-Step-Defer-Abort@L94689`, `rule.24.Cleanup-Done@L94705`, `rule.24.Destroy-Empty@L94721`, `rule.24.Destroy-Cons@L94736`, `def.24.CleanupJudgDyn@L94752`, `rule.24.Cleanup-Empty@L94765`, `rule.24.Cleanup-Cons-Drop@L94780`, `rule.24.Cleanup-Cons-Drop-Panic@L94796`
- `rule.24.Cleanup-Cons-DropStatic@L94812`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94828`, `rule.24.Cleanup-Cons-Defer-Ok@L94844`, `rule.24.Cleanup-Cons-Defer-Panic@L94860`, `def.24.CleanupScopeJudg@L94876`, `rule.24.CleanupScope-From-SmallStep@L94889`, `rule.24.Unwind-Step@L94905`, `rule.24.Unwind-Abort@L94921`
- `def.24.CleanupJudg@L94069`, `rule.24.CleanupPlan@L94082`, `def.24.EmitDropSpec@L94098`, `def.24.PanicOutAddr@L94114`, `def.24.PanicRecordOf@L94127`, `def.24.WritePanicRecord@L94140`, `def.24.InitPanicHandle@L94153`, `req.24.InitPanicHandleResponsiblePrefix@L94166`
- `rule.24.PanicSym@L94179`, `def.24.PanicReasonCodes@L94194`, `def.24.PanicSites@L94219`, `def.24.ClearPanic@L94243`, `def.24.PanicCheck@L94256`, `def.24.LowerPanic@L94269`, `def.24.ResponsibleBinding@L94284`, `grammar.24.CleanupItem@L94297`
- `def.24.DropJudgmentDefinitions@L94310`, `def.24.RecordType@L94327`, `def.24.DropCall@L94340`, `def.24.ReleaseValue@L94357`, `def.24.DropChildren@L94371`, `def.24.DropList@L94391`, `rule.24.DropAction-Moved@L94405`, `rule.24.DropAction-Partial@L94421`
- `rule.24.DropAction-Valid@L94437`, `rule.24.DropStaticAction@L94453`, `def.24.NonRecordFOk@L94469`, `rule.24.DropValueOut-DropPanic@L94482`, `rule.24.DropValueOut-ChildPanic@L94498`, `rule.24.DropValueOut-Ok@L94514`, `def.24.CleanupStateDefinitions@L94532`, `rule.24.Cleanup-Start@L94546`
- `rule.24.Cleanup-Step-Drop-Ok@L94561`, `rule.24.Cleanup-Step-Drop-Panic@L94577`, `rule.24.Cleanup-Step-Drop-Abort@L94593`, `rule.24.Cleanup-Step-DropStatic-Ok@L94609`, `rule.24.Cleanup-Step-DropStatic-Panic@L94625`, `rule.24.Cleanup-Step-DropStatic-Abort@L94641`, `rule.24.Cleanup-Step-Defer-Ok@L94657`, `rule.24.Cleanup-Step-Defer-Panic@L94673`
- `rule.24.Cleanup-Step-Defer-Abort@L94689`, `rule.24.Cleanup-Done@L94705`, `rule.24.Destroy-Empty@L94721`, `rule.24.Destroy-Cons@L94736`, `def.24.CleanupJudgDyn@L94752`, `rule.24.Cleanup-Empty@L94765`, `rule.24.Cleanup-Cons-Drop@L94780`, `rule.24.Cleanup-Cons-Drop-Panic@L94796`
- `rule.24.Cleanup-Cons-DropStatic@L94812`, `rule.24.Cleanup-Cons-DropStatic-Panic@L94828`, `rule.24.Cleanup-Cons-Defer-Ok@L94844`, `rule.24.Cleanup-Cons-Defer-Panic@L94860`, `def.24.CleanupScopeJudg@L94876`, `rule.24.CleanupScope-From-SmallStep@L94889`, `rule.24.Unwind-Step@L94905`, `rule.24.Unwind-Abort@L94921`

#### `spec.runtime-interface`

Count: 64 total; 64 required; 0 recommended; 0 informative. Ledger line span: L94519-L95514.

- `def.24.RuntimeIfcJudg@L94943`, `def.24.BuiltinModalLayoutSpec@L94956`, `rule.24.BuiltinModalLayout@L94969`, `def.24.BuiltinModalSymMap@L94985`, `rule.24.BuiltinModalSym@L95012`, `rule.24.RegionAddr-AddrIsActive@L95028`, `rule.24.RegionAddr-AddrTagFrom@L95043`, `rule.24.BuiltinSym-FileSystem-OpenRead@L95058`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L95073`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L95088`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L95103`, `rule.24.BuiltinSym-FileSystem-ReadFile@L95118`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L95133`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95148`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95163`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95178`
- `rule.24.BuiltinSym-FileSystem-Exists@L95193`, `rule.24.BuiltinSym-FileSystem-Remove@L95208`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95223`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95238`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95253`, `rule.24.BuiltinSym-FileSystem-Kind@L95268`, `rule.24.BuiltinSym-FileSystem-Restrict@L95283`, `rule.24.BuiltinSym-Network-RestrictHost@L95298`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95313`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95328`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95343`, `rule.24.BuiltinSym-Reactor-Run@L95358`, `rule.24.BuiltinSym-Reactor-Register@L95373`, `rule.24.BuiltinSym-System-Exit@L95388`, `rule.24.BuiltinSym-System-GetEnv@L95403`, `rule.24.BuiltinSym-System-Run@L95478`
- `def.24.BuiltinSymJudg@L95495`, `def.24.StringBytesBuiltinMethodSets@L95508`, `def.24.StringBuiltinSymbols@L95524`, `def.24.BytesBuiltinSymbols@L95544`, `rule.24.BuiltinSym-String-Err@L95566`, `rule.24.BuiltinSym-Bytes-Err@L95582`, `def.24.DropHookJudg@L95598`, `rule.24.StringDropSym-Decl@L95611`
- `rule.24.BytesDropSym-Decl@L95626`, `rule.24.StringDropSym-Err@L95641`, `rule.24.BytesDropSym-Err@L95657`, `def.24.RuntimeDeclJudg@L95675`, `def.24.RuntimeMethodAndSymbolSets@L95688`, `def.24.CapabilityBuiltinSigs@L95707`, `def.24.CoreRuntimeSigs@L95725`, `def.24.BuiltinModalProcSigs@L95741`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95759`, `def.24.LLVMDeclType@L95778`, `rule.24.RuntimeDecls@L95791`, `def.24.RuntimeDeclarationCoverage@L95807`, `rule.24.Prim-Network-RestrictHost-Runtime@L95826`, `def.24.HeapJudg@L95842`, `req.24.HeapHostPrimitiveRelations@L95855`, `def.24.HeapStateAccountingDefinitions@L95868`
- `req.24.HeapPrimitiveSemantics@L95883`, `rule.24.Prim-Heap-WithQuota@L95909`, `rule.24.Prim-Heap-AllocRaw@L95925`, `rule.24.Prim-Heap-DeallocRaw@L95941`, `def.24.ReactorJudg@L95957`, `req.24.ReactorHostPrimitiveRelations@L95970`, `rule.24.Prim-Reactor-Run@L95983`, `rule.24.Prim-Reactor-Register@L95999`
- `def.24.RuntimeIfcJudg@L94943`, `def.24.BuiltinModalLayoutSpec@L94956`, `rule.24.BuiltinModalLayout@L94969`, `def.24.BuiltinModalSymMap@L94985`, `rule.24.BuiltinModalSym@L95012`, `rule.24.RegionAddr-AddrIsActive@L95028`, `rule.24.RegionAddr-AddrTagFrom@L95043`, `rule.24.BuiltinSym-FileSystem-OpenRead@L95058`
- `rule.24.BuiltinSym-FileSystem-OpenWrite@L95073`, `rule.24.BuiltinSym-FileSystem-OpenAppend@L95088`, `rule.24.BuiltinSym-FileSystem-CreateWrite@L95103`, `rule.24.BuiltinSym-FileSystem-ReadFile@L95118`, `rule.24.BuiltinSym-FileSystem-ReadBytes@L95133`, `rule.24.BuiltinSym-FileSystem-WriteFile@L95148`, `rule.24.BuiltinSym-FileSystem-WriteStdout@L95163`, `rule.24.BuiltinSym-FileSystem-WriteStderr@L95178`
- `rule.24.BuiltinSym-FileSystem-Exists@L95193`, `rule.24.BuiltinSym-FileSystem-Remove@L95208`, `rule.24.BuiltinSym-FileSystem-OpenDir@L95223`, `rule.24.BuiltinSym-FileSystem-CreateDir@L95238`, `rule.24.BuiltinSym-FileSystem-EnsureDir@L95253`, `rule.24.BuiltinSym-FileSystem-Kind@L95268`, `rule.24.BuiltinSym-FileSystem-Restrict@L95283`, `rule.24.BuiltinSym-Network-RestrictHost@L95298`
- `rule.24.BuiltinSym-HeapAllocator-WithQuota@L95313`, `rule.24.BuiltinSym-HeapAllocator-AllocRaw@L95328`, `rule.24.BuiltinSym-HeapAllocator-DeallocRaw@L95343`, `rule.24.BuiltinSym-Reactor-Run@L95358`, `rule.24.BuiltinSym-Reactor-Register@L95373`, `rule.24.BuiltinSym-System-Exit@L95388`, `rule.24.BuiltinSym-System-GetEnv@L95403`, `rule.24.BuiltinSym-System-Run@L95478`
- `def.24.BuiltinSymJudg@L95495`, `def.24.StringBytesBuiltinMethodSets@L95508`, `def.24.StringBuiltinSymbols@L95524`, `def.24.BytesBuiltinSymbols@L95544`, `rule.24.BuiltinSym-String-Err@L95566`, `rule.24.BuiltinSym-Bytes-Err@L95582`, `def.24.DropHookJudg@L95598`, `rule.24.StringDropSym-Decl@L95611`
- `rule.24.BytesDropSym-Decl@L95626`, `rule.24.StringDropSym-Err@L95641`, `rule.24.BytesDropSym-Err@L95657`, `def.24.RuntimeDeclJudg@L95675`, `def.24.RuntimeMethodAndSymbolSets@L95688`, `def.24.CapabilityBuiltinSigs@L95707`, `def.24.CoreRuntimeSigs@L95725`, `def.24.BuiltinModalProcSigs@L95741`
- `def.24.RuntimeSigBuiltinModalAndMethodDispatch@L95759`, `def.24.LLVMDeclType@L95778`, `rule.24.RuntimeDecls@L95791`, `def.24.RuntimeDeclarationCoverage@L95807`, `rule.24.Prim-Network-RestrictHost-Runtime@L95826`, `def.24.HeapJudg@L95842`, `req.24.HeapHostPrimitiveRelations@L95855`, `def.24.HeapStateAccountingDefinitions@L95868`
- `req.24.HeapPrimitiveSemantics@L95883`, `rule.24.Prim-Heap-WithQuota@L95909`, `rule.24.Prim-Heap-AllocRaw@L95925`, `rule.24.Prim-Heap-DeallocRaw@L95941`, `def.24.ReactorJudg@L95957`, `req.24.ReactorHostPrimitiveRelations@L95970`, `rule.24.Prim-Reactor-Run@L95983`, `rule.24.Prim-Reactor-Register@L95999`

#### `spec.backend`

Count: 190 total; 190 required; 0 recommended; 0 informative. Ledger line span: L95536-L98637.

- `def.24.LLVMHeader@L96021`, `def.24.OpaquePointerModel@L96036`, `def.24.LLVMAttrJudg@L96058`, `rule.24.PtrStateOf-Perm@L96071`, `rule.24.LLVM-PtrAttrs-Valid@L96087`, `rule.24.LLVM-PtrAttrs-Other@L96103`, `rule.24.LLVM-PtrAttrs-RawPtr@L96119`, `rule.24.LLVM-ArgAttrs-Ptr@L96135`
- `rule.24.LLVM-ArgAttrs-RawPtr@L96152`, `rule.24.LLVM-ArgAttrs-NonPtr@L96168`, `def.24.LLVMOptionalArgumentAttrs@L96184`, `def.24.LLVMUBAndPoisonAvoidance@L96202`, `def.24.MemoryIntrinsics@L96229`, `def.24.LLVMToolchain@L96252`, `req.24.HostedCompilerLLVMVersion@L96265`, `def.24.LLVMTyJudg@L96280`
- `def.24.LLVMPrimitiveTypeHelpers@L96293`, `def.24.StructElems@L96329`, `def.24.TaggedElems@L96347`, `rule.24.LLVMTy-Prim@L96364`, `rule.24.LLVMTy-Perm@L96380`, `rule.24.LLVMTy-Refine@L96396`, `rule.24.LLVMTy-Ptr@L96412`, `rule.24.LLVMTy-RawPtr@L96428`
- `rule.24.LLVMTy-Func@L96444`, `rule.24.LLVMTy-Closure@L96460`, `rule.24.LLVMTy-Alias@L96476`, `rule.24.LLVMTy-Record@L96492`, `rule.24.LLVMTy-Tuple@L96508`, `rule.24.LLVMTy-Array@L96524`, `rule.24.LLVMTy-Slice@L96540`, `rule.24.LLVMTy-Range@L96556`
- `rule.24.LLVMTy-RangeInclusive@L96572`, `rule.24.LLVMTy-RangeFrom@L96588`, `rule.24.LLVMTy-RangeTo@L96604`, `rule.24.LLVMTy-RangeToInclusive@L96620`, `rule.24.LLVMTy-RangeFull@L96636`, `rule.24.LLVMTy-Enum@L96651`, `rule.24.LLVMTy-Union-Niche@L96667`, `rule.24.LLVMTy-Union-Tagged@L96683`
- `rule.24.LLVMTy-Modal-Niche@L96699`, `rule.24.LLVMTy-Modal-Tagged@L96715`, `rule.24.LLVMTy-Modal-StringBytes@L96731`, `rule.24.LLVMTy-ModalState@L96749`, `rule.24.LLVMTy-Dynamic@L96765`, `rule.24.LLVMTy-StringView@L96781`, `rule.24.LLVMTy-StringManaged@L96797`, `rule.24.LLVMTy-BytesView@L96813`
- `rule.24.LLVMTy-BytesManaged@L96829`, `rule.24.LLVMTy-Err@L96845`, `def.24.LowerIRJudg@L96863`, `def.24.LLVMInstrHelpers@L96876`, `rule.24.LowerIRInstr-Empty@L96907`, `rule.24.LowerIRInstr-Seq@L96922`, `def.24.MemoryInstructionHelpers@L96938`, `def.24.ConstBytesEncoding@L96955`
- `def.24.StaticTypeBySymbol@L96981`, `def.24.StateRefJudg@L96995`, `rule.24.StateRef-Session@L97009`, `rule.24.StateRef-Global@L97025`, `def.24.CallSignatureHelpers@L97041`, `def.24.ParamInitHelpers@L97060`, `rule.24.LowerIRDecl-Proc-User@L97084`, `rule.24.LowerIRDecl-Proc-Gen@L97100`
- `rule.24.LowerIRDecl-GlobalConst@L97116`, `rule.24.LowerIRDecl-GlobalZero@L97132`, `req.24.HostedStateInitializerTemplates@L97148`, `rule.24.LowerIRDecl-VTable@L97161`, `rule.24.Lower-AllocIR@L97177`, `rule.24.Lower-BindVarIR@L97193`, `rule.24.Lower-ReadVarIR@L97209`, `rule.24.Lower-ReadVarIR-Err@L97225`
- `def.24.ProcSymbol@L97241`, `rule.24.Lower-ReadPathIR-Static-User@L97254`, `rule.24.Lower-ReadPathIR-Static-Gen@L97270`, `rule.24.Lower-ReadPathIR-Proc-User@L97286`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97302`, `rule.24.Lower-ReadPathIR-Runtime@L97318`, `rule.24.Lower-ReadPathIR-Record@L97334`, `rule.24.Lower-StoreVarIR@L97350`
- `rule.24.Lower-StoreVarNoDropIR@L97366`, `rule.24.Lower-MoveStateIR@L97382`, `rule.24.Lower-StoreGlobal@L97398`, `rule.24.Lower-ReadPlaceIR@L97414`, `rule.24.Lower-WritePlaceIR@L97430`, `def.24.PtrType@L97446`, `rule.24.Lower-ReadPtrIR@L97459`, `rule.24.Lower-ReadPtrIR-Raw@L97475`
- `rule.24.Lower-ReadPtrIR-Null@L97491`, `rule.24.Lower-ReadPtrIR-Expired@L97507`, `rule.24.Lower-WritePtrIR@L97523`, `rule.24.Lower-WritePtrIR-Null@L97539`, `rule.24.Lower-WritePtrIR-Expired@L97555`, `rule.24.Lower-WritePtrIR-Raw@L97571`, `rule.24.Lower-WritePtrIR-Raw-Err@L97587`, `rule.24.Lower-AddrOfIR@L97603`
- `def.24.CallLoweringHelpers@L97619`, `rule.24.Lower-CallIR-Func@L97647`, `def.24.DynamicDispatchHelpers@L97663`, `rule.24.Lower-CallVTable@L97681`, `rule.24.LowerIRInstr-ClearPanic@L97697`, `rule.24.LowerIRInstr-PanicCheck@L97713`, `rule.24.LowerIRInstr-CheckPoison@L97729`, `rule.24.LowerIRInstr-LowerPanic@L97745`
- `def.24.IfLoweringHelpers@L97761`, `rule.24.Lower-IfIR@L97778`, `def.24.BlockCleanupLoweringHelpers@L97794`, `rule.24.Lower-BlockIR@L97810`, `def.24.StructuredIRLoweringForms@L97826`, `rule.24.Lower-LoopIR@L97854`, `rule.24.Lower-IfCaseIR@L97870`, `rule.24.Lower-RegionIR@L97886`
- `rule.24.Lower-FrameIR@L97902`, `def.24.BranchLowerForms@L97918`, `rule.24.Lower-BranchIR-Unconditional@L97932`, `rule.24.Lower-BranchIR-Conditional@L97948`, `def.24.PhiLowerForm@L97963`, `rule.24.Lower-PhiIR@L97976`, `rule.24.LowerIRDecl-Err@L97992`, `rule.24.LowerIRInstr-Err@L98008`
- `def.24.BindStorageJudg@L98026`, `def.24.BindRegionTarget@L98049`, `req.24.ResolveTargetNearestLiveAlias@L98068`, `rule.24.BindValid-Sigma@L98081`, `rule.24.BindSlot-Param-ByValue@L98097`, `rule.24.BindSlot-Param-ByRef@L98113`, `rule.24.BindSlot-Region@L98129`, `rule.24.BindSlot-Local@L98145`
- `rule.24.BindSlot-Static@L98161`, `rule.24.UpdateValid-BindVar@L98177`, `rule.24.UpdateValid-StoreVar@L98192`, `rule.24.UpdateValid-StoreVarNoDrop@L98207`, `rule.24.UpdateValid-MoveRoot@L98223`, `rule.24.UpdateValid-PartialMove-Init@L98239`, `rule.24.UpdateValid-PartialMove-Step@L98255`, `def.24.DropOnAssignHelpers@L98271`
- `rule.24.DropOnAssign-NotApplicable@L98287`, `rule.24.DropOnAssign-Record-Valid@L98303`, `rule.24.DropOnAssign-Record-Partial@L98319`, `rule.24.DropOnAssign-Record-Moved@L98335`, `rule.24.DropOnAssign-Aggregate-Ok@L98351`, `rule.24.DropOnAssign-Aggregate-Moved@L98367`, `rule.24.BindSlot-Err@L98383`, `rule.24.BindValid-Err@L98399`
- `rule.24.UpdateValid-Err@L98415`, `rule.24.DropOnAssign-Err@L98431`, `def.24.LLVMCallJudg@L98449`, `def.24.LLVMCallSigFields@L98462`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98478`, `rule.24.LLVMArgLower-ByValue-Other@L98494`, `rule.24.LLVMArgLower-ByRef@L98510`, `rule.24.LLVMRetLower-ByValue-ZST@L98526`
- `rule.24.LLVMRetLower-ByValue@L98542`, `rule.24.LLVMRetLower-SRet@L98558`, `def.24.LLVMCallArgLists@L98574`, `rule.24.LLVMCall-ByValue@L98589`, `rule.24.LLVMCall-SRet@L98605`, `def.24.ByRefAccess@L98621`, `rule.24.LLVMArgLower-Err@L98636`, `rule.24.LLVMRetLower-Err@L98652`
- `rule.24.LLVMCall-Err@L98668`, `def.24.VTableJudg@L98686`, `def.24.VTableEmissionHelpers@L98699`, `rule.24.EmitDropGlue-Decl@L98723`, `rule.24.EmitVTable-Err@L98739`, `def.24.LiteralEmitJudg@L98757`, `def.24.StringBytesAndRawBytes@L98770`, `rule.24.EmitLiteralData-Decl@L98793`
- `rule.24.EmitLiteral-String@L98809`, `req.24.EmitLiteral-String-Utf8Valid@L98825`, `rule.24.EmitLiteral-Bytes@L98838`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98854`, `rule.24.EmitLiteral-Char@L98867`, `rule.24.EmitLiteral-Int@L98883`, `rule.24.EmitLiteral-Float@L98899`, `rule.24.EmitLiteral-Err@L98915`
- `def.24.PoisonJudg@L98933`, `def.24.PoisonSet@L98946`, `rule.24.PoisonFlag-Decl@L98959`, `def.24.PoisonFlagStorage@L98974`, `req.24.HostedPoisonFlagTemplate@L98988`, `rule.24.CheckPoison-Use@L99001`, `sem.24.CheckPoisonBehavior@L99017`, `req.24.HostedPoisonStateIsolation@L99030`
- `rule.24.SetPoison-OnInitFail@L99043`, `rule.24.PoisonFlag-Err@L99059`, `rule.24.CheckPoison-Err@L99075`, `rule.24.SetPoison-Err@L99091`, `req.24.OutputBackendDiagnosticsOwnership@L99109`, `diag.24.OutputBackendDiagnostics@L99122`
- `def.24.LLVMHeader@L96021`, `def.24.OpaquePointerModel@L96036`, `def.24.LLVMAttrJudg@L96058`, `rule.24.PtrStateOf-Perm@L96071`, `rule.24.LLVM-PtrAttrs-Valid@L96087`, `rule.24.LLVM-PtrAttrs-Other@L96103`, `rule.24.LLVM-PtrAttrs-RawPtr@L96119`, `rule.24.LLVM-ArgAttrs-Ptr@L96135`
- `rule.24.LLVM-ArgAttrs-RawPtr@L96152`, `rule.24.LLVM-ArgAttrs-NonPtr@L96168`, `def.24.LLVMOptionalArgumentAttrs@L96184`, `def.24.LLVMUBAndPoisonAvoidance@L96202`, `def.24.MemoryIntrinsics@L96229`, `def.24.LLVMToolchain@L96252`, `req.24.HostedCompilerLLVMVersion@L96265`, `def.24.LLVMTyJudg@L96280`
- `def.24.LLVMPrimitiveTypeHelpers@L96293`, `def.24.StructElems@L96329`, `def.24.TaggedElems@L96347`, `rule.24.LLVMTy-Prim@L96364`, `rule.24.LLVMTy-Perm@L96380`, `rule.24.LLVMTy-Refine@L96396`, `rule.24.LLVMTy-Ptr@L96412`, `rule.24.LLVMTy-RawPtr@L96428`
- `rule.24.LLVMTy-Func@L96444`, `rule.24.LLVMTy-Closure@L96460`, `rule.24.LLVMTy-Alias@L96476`, `rule.24.LLVMTy-Record@L96492`, `rule.24.LLVMTy-Tuple@L96508`, `rule.24.LLVMTy-Array@L96524`, `rule.24.LLVMTy-Slice@L96540`, `rule.24.LLVMTy-Range@L96556`
- `rule.24.LLVMTy-RangeInclusive@L96572`, `rule.24.LLVMTy-RangeFrom@L96588`, `rule.24.LLVMTy-RangeTo@L96604`, `rule.24.LLVMTy-RangeToInclusive@L96620`, `rule.24.LLVMTy-RangeFull@L96636`, `rule.24.LLVMTy-Enum@L96651`, `rule.24.LLVMTy-Union-Niche@L96667`, `rule.24.LLVMTy-Union-Tagged@L96683`
- `rule.24.LLVMTy-Modal-Niche@L96699`, `rule.24.LLVMTy-Modal-Tagged@L96715`, `rule.24.LLVMTy-Modal-StringBytes@L96731`, `rule.24.LLVMTy-ModalState@L96749`, `rule.24.LLVMTy-Dynamic@L96765`, `rule.24.LLVMTy-StringView@L96781`, `rule.24.LLVMTy-StringManaged@L96797`, `rule.24.LLVMTy-BytesView@L96813`
- `rule.24.LLVMTy-BytesManaged@L96829`, `rule.24.LLVMTy-Err@L96845`, `def.24.LowerIRJudg@L96863`, `def.24.LLVMInstrHelpers@L96876`, `rule.24.LowerIRInstr-Empty@L96907`, `rule.24.LowerIRInstr-Seq@L96922`, `def.24.MemoryInstructionHelpers@L96938`, `def.24.ConstBytesEncoding@L96955`
- `def.24.StaticTypeBySymbol@L96981`, `def.24.StateRefJudg@L96995`, `rule.24.StateRef-Session@L97009`, `rule.24.StateRef-Global@L97025`, `def.24.CallSignatureHelpers@L97041`, `def.24.ParamInitHelpers@L97060`, `rule.24.LowerIRDecl-Proc-User@L97084`, `rule.24.LowerIRDecl-Proc-Gen@L97100`
- `rule.24.LowerIRDecl-GlobalConst@L97116`, `rule.24.LowerIRDecl-GlobalZero@L97132`, `req.24.HostedStateInitializerTemplates@L97148`, `rule.24.LowerIRDecl-VTable@L97161`, `rule.24.Lower-AllocIR@L97177`, `rule.24.Lower-BindVarIR@L97193`, `rule.24.Lower-ReadVarIR@L97209`, `rule.24.Lower-ReadVarIR-Err@L97225`
- `def.24.ProcSymbol@L97241`, `rule.24.Lower-ReadPathIR-Static-User@L97254`, `rule.24.Lower-ReadPathIR-Static-Gen@L97270`, `rule.24.Lower-ReadPathIR-Proc-User@L97286`, `rule.24.Lower-ReadPathIR-Proc-Gen@L97302`, `rule.24.Lower-ReadPathIR-Runtime@L97318`, `rule.24.Lower-ReadPathIR-Record@L97334`, `rule.24.Lower-StoreVarIR@L97350`
- `rule.24.Lower-StoreVarNoDropIR@L97366`, `rule.24.Lower-MoveStateIR@L97382`, `rule.24.Lower-StoreGlobal@L97398`, `rule.24.Lower-ReadPlaceIR@L97414`, `rule.24.Lower-WritePlaceIR@L97430`, `def.24.PtrType@L97446`, `rule.24.Lower-ReadPtrIR@L97459`, `rule.24.Lower-ReadPtrIR-Raw@L97475`
- `rule.24.Lower-ReadPtrIR-Null@L97491`, `rule.24.Lower-ReadPtrIR-Expired@L97507`, `rule.24.Lower-WritePtrIR@L97523`, `rule.24.Lower-WritePtrIR-Null@L97539`, `rule.24.Lower-WritePtrIR-Expired@L97555`, `rule.24.Lower-WritePtrIR-Raw@L97571`, `rule.24.Lower-WritePtrIR-Raw-Err@L97587`, `rule.24.Lower-AddrOfIR@L97603`
- `def.24.CallLoweringHelpers@L97619`, `rule.24.Lower-CallIR-Func@L97647`, `def.24.DynamicDispatchHelpers@L97663`, `rule.24.Lower-CallVTable@L97681`, `rule.24.LowerIRInstr-ClearPanic@L97697`, `rule.24.LowerIRInstr-PanicCheck@L97713`, `rule.24.LowerIRInstr-CheckPoison@L97729`, `rule.24.LowerIRInstr-LowerPanic@L97745`
- `def.24.IfLoweringHelpers@L97761`, `rule.24.Lower-IfIR@L97778`, `def.24.BlockCleanupLoweringHelpers@L97794`, `rule.24.Lower-BlockIR@L97810`, `def.24.StructuredIRLoweringForms@L97826`, `rule.24.Lower-LoopIR@L97854`, `rule.24.Lower-IfCaseIR@L97870`, `rule.24.Lower-RegionIR@L97886`
- `rule.24.Lower-FrameIR@L97902`, `def.24.BranchLowerForms@L97918`, `rule.24.Lower-BranchIR-Unconditional@L97932`, `rule.24.Lower-BranchIR-Conditional@L97948`, `def.24.PhiLowerForm@L97963`, `rule.24.Lower-PhiIR@L97976`, `rule.24.LowerIRDecl-Err@L97992`, `rule.24.LowerIRInstr-Err@L98008`
- `def.24.BindStorageJudg@L98026`, `def.24.BindRegionTarget@L98049`, `req.24.ResolveTargetNearestLiveAlias@L98068`, `rule.24.BindValid-Sigma@L98081`, `rule.24.BindSlot-Param-ByValue@L98097`, `rule.24.BindSlot-Param-ByRef@L98113`, `rule.24.BindSlot-Region@L98129`, `rule.24.BindSlot-Local@L98145`
- `rule.24.BindSlot-Static@L98161`, `rule.24.UpdateValid-BindVar@L98177`, `rule.24.UpdateValid-StoreVar@L98192`, `rule.24.UpdateValid-StoreVarNoDrop@L98207`, `rule.24.UpdateValid-MoveRoot@L98223`, `rule.24.UpdateValid-PartialMove-Init@L98239`, `rule.24.UpdateValid-PartialMove-Step@L98255`, `def.24.DropOnAssignHelpers@L98271`
- `rule.24.DropOnAssign-NotApplicable@L98287`, `rule.24.DropOnAssign-Record-Valid@L98303`, `rule.24.DropOnAssign-Record-Partial@L98319`, `rule.24.DropOnAssign-Record-Moved@L98335`, `rule.24.DropOnAssign-Aggregate-Ok@L98351`, `rule.24.DropOnAssign-Aggregate-Moved@L98367`, `rule.24.BindSlot-Err@L98383`, `rule.24.BindValid-Err@L98399`
- `rule.24.UpdateValid-Err@L98415`, `rule.24.DropOnAssign-Err@L98431`, `def.24.LLVMCallJudg@L98449`, `def.24.LLVMCallSigFields@L98462`, `rule.24.LLVMArgLower-ByValue-PtrValid@L98478`, `rule.24.LLVMArgLower-ByValue-Other@L98494`, `rule.24.LLVMArgLower-ByRef@L98510`, `rule.24.LLVMRetLower-ByValue-ZST@L98526`
- `rule.24.LLVMRetLower-ByValue@L98542`, `rule.24.LLVMRetLower-SRet@L98558`, `def.24.LLVMCallArgLists@L98574`, `rule.24.LLVMCall-ByValue@L98589`, `rule.24.LLVMCall-SRet@L98605`, `def.24.ByRefAccess@L98621`, `rule.24.LLVMArgLower-Err@L98636`, `rule.24.LLVMRetLower-Err@L98652`
- `rule.24.LLVMCall-Err@L98668`, `def.24.VTableJudg@L98686`, `def.24.VTableEmissionHelpers@L98699`, `rule.24.EmitDropGlue-Decl@L98723`, `rule.24.EmitVTable-Err@L98739`, `def.24.LiteralEmitJudg@L98757`, `def.24.StringBytesAndRawBytes@L98770`, `rule.24.EmitLiteralData-Decl@L98793`
- `rule.24.EmitLiteral-String@L98809`, `req.24.EmitLiteral-String-Utf8Valid@L98825`, `rule.24.EmitLiteral-Bytes@L98838`, `req.24.EmitLiteral-Bytes-UndefinedRawBytes@L98854`, `rule.24.EmitLiteral-Char@L98867`, `rule.24.EmitLiteral-Int@L98883`, `rule.24.EmitLiteral-Float@L98899`, `rule.24.EmitLiteral-Err@L98915`
- `def.24.PoisonJudg@L98933`, `def.24.PoisonSet@L98946`, `rule.24.PoisonFlag-Decl@L98959`, `def.24.PoisonFlagStorage@L98974`, `req.24.HostedPoisonFlagTemplate@L98988`, `rule.24.CheckPoison-Use@L99001`, `sem.24.CheckPoisonBehavior@L99017`, `req.24.HostedPoisonStateIsolation@L99030`
- `rule.24.SetPoison-OnInitFail@L99043`, `rule.24.PoisonFlag-Err@L99059`, `rule.24.CheckPoison-Err@L99075`, `rule.24.SetPoison-Err@L99091`, `req.24.OutputBackendDiagnosticsOwnership@L99109`, `diag.24.OutputBackendDiagnostics@L99122`

### Lowering, Backend, Runtime Interface, And Driver

#### `backend.llvm-target`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L5343-L5373.

- `def.LLVMTargetConstants@L5343`, `def.IsRootModule@L5359`, `def.WithEntry@L5373`

#### `backend.llvm-codegen`

Count: 4 total; 4 required; 0 recommended; 0 informative. Ledger line span: L5389-L6747.

- `CodegenObj-LLVM@L5389`, `CodegenIR-LLVM@L5407`, `AssembleIR-Ok@L6729`, `AssembleIR-Err@L6747`

#### `lowering.attributes`

Count: 2 total; 2 required; 0 recommended; 0 informative. Ledger line span: L27099-L27308.

- `conformance.AttributeLoweringOwnership@L27173`, `conformance.VendorAttributeLowering@L27382`
- `conformance.AttributeLoweringOwnership@L27173`, `conformance.VendorAttributeLowering@L27382`

#### `lowering.attributes.layout`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27595-L27595.

- `conformance.LayoutAttributeLowering@L27669`
- `conformance.LayoutAttributeLowering@L27669`

#### `lowering.attributes.optimization`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L27740-L27740.

- `conformance.OptimizationAttributeLowering@L27814`
- `conformance.OptimizationAttributeLowering@L27814`

#### `lowering.attributes.metadata`

Count: 1 total; 1 required; 0 recommended; 0 informative. Ledger line span: L28221-L28221.

- `conformance.DiagnosticsMetadataLowering@L28295`
- `conformance.DiagnosticsMetadataLowering@L28295`

#### `lowering.permissions`

Count: 3 total; 3 required; 0 recommended; 0 informative. Ledger line span: L28471-L28686.

- `conformance.PermissionLayoutNeutrality@L28830`, `req.PermissionFormsLoweringDiagnostics@L28892`, `conformance.AliasExclusivityLowering@L29045`
- `conformance.PermissionLayoutNeutrality@L28830`, `req.PermissionFormsLoweringDiagnostics@L28892`, `conformance.AliasExclusivityLowering@L29045`

#### `codegen`

Count: 51 total; 51 required; 0 recommended; 0 informative. Ledger line span: L29113-L40918.

- `conformance.PermissionAdmissibilityLowering@L29472`, `conformance.ImportDeclarationLowering@L29707`, `conformance.UsingDeclarationLowering@L30214`, `def.ConstInitJudgementSet@L30507`, `def.ConstInitLiteralEncoding@L30523`, `def.StaticName@L30537`, `def.StaticBindingFunctionSignature@L30581`, `def.StaticSym@L30595`
- `def.InitSym@L30665`, `def.DeinitSym@L30697`, `def.StaticSymPath@L30757`, `def.StaticAddr@L30771`, `def.AddrOfSym@L30799`, `def.SeqIRList@L30841`, `def.StaticStoreIR@L30856`, `def.Rev@L30960`
- `conformance.ExternBlockLowering@L31405`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34602`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34618`, `def.PrimitiveValueBits@L34949`, `req.PrimitiveLayoutAbiOwnership@L34969`, `def.TupleFields@L35393`, `def.TupleLayoutJudgementSet@L35704`, `def.TupleValueBits@L35809`
- `def.ArrayLen@L36498`, `def.ArrayValueBits@L36512`, `def.SliceValueBits@L37067`, `def.LoweringChecksJudgementSet@L37729`, `def.RangeValueBits@L37745`, `def.RecordLayoutHelpers@L38905`, `def.FieldOffset@L39017`, `def.FieldValueList@L39031`
- `def.StructBits@L39045`, `def.PadBytes@L39059`, `def.RecordValueBits@L39073`, `def.EnumLayoutHelpers@L40083`, `def.EnumPayloadBits@L40184`, `def.EnumValueBits@L40200`, `def.UnionNicheOrderingHelpers@L40553`, `def.UnionTypeOrderingKeys@L40573`
- `def.TypeKey@L40615`, `def.TypeKeyOrdering@L40648`, `def.UnionMemberLayoutSelection@L40670`, `def.UnionLayoutHelpers@L40690`, `def.UnionNicheBits@L40799`, `def.UnionPayloadBits@L40813`, `def.TaggedBits@L40827`, `def.UnionTaggedBits@L40843`
- `def.UnionBits@L40857`, `def.UnionValueBits@L40871`, `def.TypeAliasValueBits@L41277`
- `conformance.PermissionAdmissibilityLowering@L29472`, `conformance.ImportDeclarationLowering@L29707`, `conformance.UsingDeclarationLowering@L30214`, `def.ConstInitJudgementSet@L30507`, `def.ConstInitLiteralEncoding@L30523`, `def.StaticName@L30537`, `def.StaticBindingFunctionSignature@L30581`, `def.StaticSym@L30595`
- `def.InitSym@L30665`, `def.DeinitSym@L30697`, `def.StaticSymPath@L30757`, `def.StaticAddr@L30771`, `def.AddrOfSym@L30799`, `def.SeqIRList@L30841`, `def.StaticStoreIR@L30856`, `def.Rev@L30960`
- `conformance.ExternBlockLowering@L31405`, `conformance.ModuleAggregationEagerGraphLoweringInput@L34602`, `conformance.ModuleAggregationLifecycleLoweringOwnership@L34618`, `def.PrimitiveValueBits@L34949`, `req.PrimitiveLayoutAbiOwnership@L34969`, `def.TupleFields@L35393`, `def.TupleLayoutJudgementSet@L35704`, `def.TupleValueBits@L35809`
- `def.ArrayLen@L36498`, `def.ArrayValueBits@L36512`, `def.SliceValueBits@L37067`, `def.LoweringChecksJudgementSet@L37729`, `def.RangeValueBits@L37745`, `def.RecordLayoutHelpers@L38905`, `def.FieldOffset@L39017`, `def.FieldValueList@L39031`
- `def.StructBits@L39045`, `def.PadBytes@L39059`, `def.RecordValueBits@L39073`, `def.EnumLayoutHelpers@L40083`, `def.EnumPayloadBits@L40184`, `def.EnumValueBits@L40200`, `def.UnionNicheOrderingHelpers@L40553`, `def.UnionTypeOrderingKeys@L40573`
- `def.TypeKey@L40615`, `def.TypeKeyOrdering@L40648`, `def.UnionMemberLayoutSelection@L40670`, `def.UnionLayoutHelpers@L40690`, `def.UnionNicheBits@L40799`, `def.UnionPayloadBits@L40813`, `def.TaggedBits@L40827`, `def.UnionTaggedBits@L40843`
- `def.UnionBits@L40857`, `def.UnionValueBits@L40871`, `def.TypeAliasValueBits@L41277`
